// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/print_web_view_helper.h"

#include <string>

#include "base/auto_reset.h"
#include "base/command_line.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/process_util.h"
#include "base/stringprintf.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/print_messages.h"
#include "chrome/common/render_messages.h"
#include "chrome/renderer/prerender/prerender_helper.h"
#include "content/public/renderer/render_thread.h"
#include "content/public/renderer/render_view.h"
#include "grit/browser_resources.h"
#include "grit/generated_resources.h"
#include "printing/metafile.h"
#include "printing/metafile_impl.h"
#include "printing/units.h"
#include "skia/ext/vector_platform_device_skia.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebSize.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebURLRequest.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebConsoleMessage.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebDocument.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebElement.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFrame.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebPlugin.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebPluginDocument.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebPrintParams.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebPrintScalingOption.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebScriptSource.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebSettings.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebView.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "webkit/glue/webpreferences.h"

namespace {

const double kMinDpi = 1.0;

const char kPageLoadScriptFormat[] =
    "document.open(); document.write(%s); document.close();";

const char kPageSetupScriptFormat[] = "setup(%s);";

void ExecuteScript(WebKit::WebFrame* frame,
                   const char* script_format,
                   const base::Value& parameters) {
  std::string json;
  base::JSONWriter::Write(&parameters, &json);
  std::string script = StringPrintf(script_format, json.c_str());
  frame->executeScript(WebKit::WebString(UTF8ToUTF16(script)));
}

int GetDPI(const PrintMsg_Print_Params* print_params) {
#if defined(OS_MACOSX)
  // On the Mac, the printable area is in points, don't do any scaling based
  // on dpi.
  return printing::kPointsPerInch;
#else
  return static_cast<int>(print_params->dpi);
#endif  // defined(OS_MACOSX)
}

bool PrintMsg_Print_Params_IsValid(const PrintMsg_Print_Params& params) {
  return !params.content_size.IsEmpty() && !params.page_size.IsEmpty() &&
         !params.printable_area.IsEmpty() && params.document_cookie &&
         params.desired_dpi && params.max_shrink && params.min_shrink &&
         params.dpi && (params.margin_top >= 0) && (params.margin_left >= 0);
}

bool PageLayoutIsEqual(const PrintMsg_PrintPages_Params& oldParams,
                       const PrintMsg_PrintPages_Params& newParams) {
  return oldParams.params.content_size == newParams.params.content_size &&
         oldParams.params.printable_area == newParams.params.printable_area &&
         oldParams.params.page_size == newParams.params.page_size &&
         oldParams.params.margin_top == newParams.params.margin_top &&
         oldParams.params.margin_left == newParams.params.margin_left &&
         oldParams.params.desired_dpi == newParams.params.desired_dpi &&
         oldParams.params.dpi == newParams.params.dpi;
}

bool PrintMsg_Print_Params_IsEqual(
    const PrintMsg_PrintPages_Params& oldParams,
    const PrintMsg_PrintPages_Params& newParams) {
  return PageLayoutIsEqual(oldParams, newParams) &&
         oldParams.params.max_shrink == newParams.params.max_shrink &&
         oldParams.params.min_shrink == newParams.params.min_shrink &&
         oldParams.params.selection_only == newParams.params.selection_only &&
         oldParams.params.supports_alpha_blend ==
             newParams.params.supports_alpha_blend &&
         oldParams.pages.size() == newParams.pages.size() &&
         oldParams.params.print_to_pdf == newParams.params.print_to_pdf &&
         oldParams.params.print_scaling_option ==
             newParams.params.print_scaling_option &&
         oldParams.params.display_header_footer ==
             newParams.params.display_header_footer &&
         oldParams.params.date == newParams.params.date &&
         oldParams.params.title == newParams.params.title &&
         oldParams.params.url == newParams.params.url &&
         std::equal(oldParams.pages.begin(), oldParams.pages.end(),
             newParams.pages.begin());
}

PrintMsg_Print_Params GetCssPrintParams(
    WebKit::WebFrame* frame,
    int page_index,
    const PrintMsg_Print_Params& page_params) {
  PrintMsg_Print_Params page_css_params = page_params;
  int dpi = GetDPI(&page_params);

  using printing::ConvertUnit;
  using printing::kPixelsPerInch;
  WebKit::WebSize page_size_in_pixels(
      ConvertUnit(page_params.page_size.width(), dpi, kPixelsPerInch),
      ConvertUnit(page_params.page_size.height(), dpi, kPixelsPerInch));
  int margin_top_in_pixels =
      ConvertUnit(page_params.margin_top, dpi, kPixelsPerInch);
  int margin_right_in_pixels = ConvertUnit(
      page_params.page_size.width() -
      page_params.content_size.width() - page_params.margin_left,
      dpi, kPixelsPerInch);
  int margin_bottom_in_pixels = ConvertUnit(
      page_params.page_size.height() -
      page_params.content_size.height() - page_params.margin_top,
      dpi, kPixelsPerInch);
  int margin_left_in_pixels = ConvertUnit(
      page_params.margin_left,
      dpi, kPixelsPerInch);

  WebKit::WebSize original_page_size_in_pixels = page_size_in_pixels;

  if (frame) {
    frame->pageSizeAndMarginsInPixels(page_index,
                                      page_size_in_pixels,
                                      margin_top_in_pixels,
                                      margin_right_in_pixels,
                                      margin_bottom_in_pixels,
                                      margin_left_in_pixels);
  }

  int new_content_width = page_size_in_pixels.width -
                          margin_left_in_pixels - margin_right_in_pixels;
  int new_content_height = page_size_in_pixels.height -
                           margin_top_in_pixels - margin_bottom_in_pixels;

  // Invalid page size and/or margins. We just use the default setting.
  if (new_content_width < 1 || new_content_height < 1) {
    CHECK(frame != NULL);
    page_css_params = GetCssPrintParams(NULL, page_index, page_params);
    return page_css_params;
  }

  page_css_params.content_size = gfx::Size(
      ConvertUnit(new_content_width, kPixelsPerInch, dpi),
      ConvertUnit(new_content_height, kPixelsPerInch, dpi));

  if (original_page_size_in_pixels != page_size_in_pixels) {
    page_css_params.page_size = gfx::Size(
        ConvertUnit(page_size_in_pixels.width, kPixelsPerInch, dpi),
        ConvertUnit(page_size_in_pixels.height, kPixelsPerInch, dpi));
  } else {
    // Printing frame doesn't have any page size css. Pixels to dpi conversion
    // causes rounding off errors. Therefore use the default page size values
    // directly.
    page_css_params.page_size = page_params.page_size;
  }

  page_css_params.margin_top =
      ConvertUnit(margin_top_in_pixels, kPixelsPerInch, dpi);
  page_css_params.margin_left =
      ConvertUnit(margin_left_in_pixels, kPixelsPerInch, dpi);
  return page_css_params;
}

double FitPrintParamsToPage(const PrintMsg_Print_Params& page_params,
                            PrintMsg_Print_Params* params_to_fit) {
  double content_width =
      static_cast<double>(params_to_fit->content_size.width());
  double content_height =
      static_cast<double>(params_to_fit->content_size.height());
  int default_page_size_height = page_params.page_size.height();
  int default_page_size_width = page_params.page_size.width();
  int css_page_size_height = params_to_fit->page_size.height();
  int css_page_size_width = params_to_fit->page_size.width();

  double scale_factor = 1.0f;
  if (page_params.page_size == params_to_fit->page_size)
    return scale_factor;

  if (default_page_size_width < css_page_size_width ||
      default_page_size_height < css_page_size_height) {
    double ratio_width =
        static_cast<double>(default_page_size_width) / css_page_size_width;
    double ratio_height =
        static_cast<double>(default_page_size_height) / css_page_size_height;
    scale_factor = ratio_width < ratio_height ? ratio_width : ratio_height;
    content_width *= scale_factor;
    content_height *= scale_factor;
  }
  params_to_fit->margin_top = static_cast<int>(
      (default_page_size_height - css_page_size_height * scale_factor) / 2 +
      (params_to_fit->margin_top * scale_factor));
  params_to_fit->margin_left = static_cast<int>(
      (default_page_size_width - css_page_size_width * scale_factor) / 2 +
      (params_to_fit->margin_left * scale_factor));
  params_to_fit->content_size = gfx::Size(
      static_cast<int>(content_width), static_cast<int>(content_height));
  params_to_fit->page_size = page_params.page_size;
  return scale_factor;
}

void CalculatePageLayoutFromPrintParams(
    const PrintMsg_Print_Params& params,
    printing::PageSizeMargins* page_layout_in_points) {
  int dpi = GetDPI(&params);
  int content_width = params.content_size.width();
  int content_height = params.content_size.height();

  int margin_bottom = params.page_size.height() -
                      content_height - params.margin_top;
  int margin_right = params.page_size.width() -
                      content_width - params.margin_left;

  using printing::ConvertUnit;
  using printing::kPointsPerInch;
  page_layout_in_points->content_width =
      ConvertUnit(content_width, dpi, kPointsPerInch);
  page_layout_in_points->content_height =
      ConvertUnit(content_height, dpi, kPointsPerInch);
  page_layout_in_points->margin_top =
      ConvertUnit(params.margin_top, dpi, kPointsPerInch);
  page_layout_in_points->margin_right =
      ConvertUnit(margin_right, dpi, kPointsPerInch);
  page_layout_in_points->margin_bottom =
      ConvertUnit(margin_bottom, dpi, kPointsPerInch);
  page_layout_in_points->margin_left =
      ConvertUnit(params.margin_left, dpi, kPointsPerInch);
}

void EnsureOrientationMatches(const PrintMsg_Print_Params& css_params,
                              PrintMsg_Print_Params* page_params) {
  if ((page_params->page_size.width() > page_params->page_size.height()) ==
      (css_params.page_size.width() > css_params.page_size.height())) {
    return;
  }

  // Swap the |width| and |height| values.
  page_params->page_size.SetSize(page_params->page_size.height(),
                                 page_params->page_size.width());
  page_params->content_size.SetSize(page_params->content_size.height(),
                                    page_params->content_size.width());
  page_params->printable_area.set_size(
      gfx::Size(page_params->printable_area.height(),
                page_params->printable_area.width()));
}

void ComputeWebKitPrintParamsInDesiredDpi(
    const PrintMsg_Print_Params& print_params,
    WebKit::WebPrintParams* webkit_print_params) {
  int dpi = GetDPI(&print_params);
  webkit_print_params->printerDPI = dpi;
  webkit_print_params->printScalingOption = print_params.print_scaling_option;

  using printing::ConvertUnit;
  webkit_print_params->printContentArea.width =
      ConvertUnit(print_params.content_size.width(), dpi,
                  print_params.desired_dpi);
  webkit_print_params->printContentArea.height =
      ConvertUnit(print_params.content_size.height(), dpi,
                  print_params.desired_dpi);

  webkit_print_params->printableArea.x =
      ConvertUnit(print_params.printable_area.x(), dpi,
                  print_params.desired_dpi);
  webkit_print_params->printableArea.y =
      ConvertUnit(print_params.printable_area.y(), dpi,
                  print_params.desired_dpi);
  webkit_print_params->printableArea.width =
      ConvertUnit(print_params.printable_area.width(), dpi,
                  print_params.desired_dpi);
  webkit_print_params->printableArea.height =
      ConvertUnit(print_params.printable_area.height(),
                  dpi, print_params.desired_dpi);

  webkit_print_params->paperSize.width =
      ConvertUnit(print_params.page_size.width(), dpi,
                  print_params.desired_dpi);
  webkit_print_params->paperSize.height =
      ConvertUnit(print_params.page_size.height(), dpi,
                  print_params.desired_dpi);
}

bool PrintingNodeOrPdfFrame(const WebKit::WebFrame* frame,
                            const WebKit::WebNode& node) {
  if (!node.isNull())
    return true;
  if (!frame->document().isPluginDocument())
    return false;
  WebKit::WebPlugin* plugin =
      frame->document().to<WebKit::WebPluginDocument>().plugin();
  return plugin && plugin->supportsPaginatedPrint();
}

bool PrintingFrameHasPageSizeStyle(WebKit::WebFrame* frame,
                                   int total_page_count) {
  if (!frame)
    return false;
  bool frame_has_custom_page_size_style = false;
  for (int i = 0; i < total_page_count; ++i) {
    if (frame->hasCustomPageSizeStyle(i)) {
      frame_has_custom_page_size_style = true;
      break;
    }
  }
  return frame_has_custom_page_size_style;
}

printing::MarginType GetMarginsForPdf(WebKit::WebFrame* frame,
                                      const WebKit::WebNode& node) {
  if (frame->isPrintScalingDisabledForPlugin(node))
    return printing::NO_MARGINS;
  else
    return printing::PRINTABLE_AREA_MARGINS;
}

bool FitToPageEnabled(const DictionaryValue& job_settings) {
  bool fit_to_paper_size = false;
  if (!job_settings.GetBoolean(printing::kSettingFitToPageEnabled,
                               &fit_to_paper_size)) {
    NOTREACHED();
  }
  return fit_to_paper_size;
}

PrintMsg_Print_Params CalculatePrintParamsForCss(
    WebKit::WebFrame* frame,
    int page_index,
    const PrintMsg_Print_Params& page_params,
    bool ignore_css_margins,
    bool fit_to_page,
    double* scale_factor) {
  PrintMsg_Print_Params css_params = GetCssPrintParams(frame, page_index,
                                                       page_params);

  PrintMsg_Print_Params params = page_params;
  EnsureOrientationMatches(css_params, &params);

  if (ignore_css_margins && fit_to_page)
    return params;

  PrintMsg_Print_Params result_params = css_params;
  if (ignore_css_margins) {
    result_params.margin_top = params.margin_top;
    result_params.margin_left = params.margin_left;

    DCHECK(!fit_to_page);
    // Since we are ignoring the margins, the css page size is no longer
    // valid.
    int default_margin_right = params.page_size.width() -
        params.content_size.width() - params.margin_left;
    int default_margin_bottom = params.page_size.height() -
        params.content_size.height() - params.margin_top;
    result_params.content_size = gfx::Size(
        result_params.page_size.width() - result_params.margin_left -
            default_margin_right,
        result_params.page_size.height() - result_params.margin_top -
            default_margin_bottom);
  }

  if (fit_to_page) {
    double factor = FitPrintParamsToPage(params, &result_params);
    if (scale_factor)
      *scale_factor = factor;
  }
  return result_params;
}

bool IsPrintPreviewEnabled() {
  return CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kRendererPrintPreview);
}

bool IsPrintThrottlingDisabled() {
  return CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kDisableScriptedPrintThrottling);
}

}  // namespace

// static - Not anonymous so that platform implementations can use it.
void PrintWebViewHelper::PrintHeaderAndFooter(
    WebKit::WebCanvas* canvas,
    int page_number,
    int total_pages,
    float webkit_scale_factor,
    const printing::PageSizeMargins& page_layout,
    const DictionaryValue& header_footer_info,
    const PrintMsg_Print_Params& params) {
  skia::VectorPlatformDeviceSkia* device =
      static_cast<skia::VectorPlatformDeviceSkia*>(canvas->getTopDevice());
  device->setDrawingArea(SkPDFDevice::kMargin_DrawingArea);

  SkAutoCanvasRestore auto_restore(canvas, true);
  canvas->scale(1 / webkit_scale_factor, 1 / webkit_scale_factor);

  WebKit::WebSize page_size(page_layout.margin_left + page_layout.margin_right +
                            page_layout.content_width,
                            page_layout.margin_top + page_layout.margin_bottom +
                            page_layout.content_height);

  WebKit::WebView* web_view = WebKit::WebView::create(NULL);
  web_view->settings()->setJavaScriptEnabled(true);
  web_view->initializeMainFrame(NULL);

  WebKit::WebFrame* frame = web_view->mainFrame();

  base::StringValue html(
      ResourceBundle::GetSharedInstance().GetLocalizedString(
          IDR_PRINT_PREVIEW_PAGE));
  // Load page with script to avoid async operations.
  ExecuteScript(frame, kPageLoadScriptFormat, html);

  scoped_ptr<base::DictionaryValue> options(header_footer_info.DeepCopy());
  options->SetDouble("width", page_size.width);
  options->SetDouble("height", page_size.height);
  options->SetDouble("topMargin", page_layout.margin_top);
  options->SetDouble("bottomMargin", page_layout.margin_bottom);
  options->SetString("pageNumber",
                     StringPrintf("%d/%d", page_number, total_pages));

  ExecuteScript(frame, kPageSetupScriptFormat, *options);

  WebKit::WebPrintParams webkit_params(page_size);
  webkit_params.printerDPI = GetDPI(&params);

  frame->printBegin(webkit_params, WebKit::WebNode(), NULL);
  frame->printPage(0, canvas);
  frame->printEnd();

  web_view->close();

  device->setDrawingArea(SkPDFDevice::kContent_DrawingArea);
}

// static - Not anonymous so that platform implementations can use it.
float PrintWebViewHelper::RenderPageContent(WebKit::WebFrame* frame,
                                            int page_number,
                                            const gfx::Rect& canvas_area,
                                            const gfx::Rect& content_area,
                                            double scale_factor,
                                            WebKit::WebCanvas* canvas) {
  SkAutoCanvasRestore auto_restore(canvas, true);
  if (content_area != canvas_area) {
    canvas->translate((content_area.x() - canvas_area.x()) / scale_factor,
                      (content_area.y() - canvas_area.y()) / scale_factor);
    SkRect clip_rect(
        SkRect::MakeXYWH(content_area.origin().x() / scale_factor,
                         content_area.origin().y() / scale_factor,
                         content_area.size().width() / scale_factor,
                         content_area.size().height() / scale_factor));
    SkIRect clip_int_rect;
    clip_rect.roundOut(&clip_int_rect);
    SkRegion clip_region(clip_int_rect);
    canvas->setClipRegion(clip_region);
  }
  return frame->printPage(page_number, canvas);
}

PrepareFrameAndViewForPrint::PrepareFrameAndViewForPrint(
    const PrintMsg_Print_Params& print_params,
    WebKit::WebFrame* frame,
    const WebKit::WebNode& node)
        : frame_(frame),
          node_to_print_(node),
          web_view_(frame->view()),
          expected_pages_count_(0),
          use_browser_overlays_(true),
          finished_(false) {
  WebKit::WebPrintParams webkit_print_params;
  ComputeWebKitPrintParamsInDesiredDpi(print_params, &webkit_print_params);

  if (WebKit::WebFrame* web_frame = web_view_->mainFrame())
    prev_scroll_offset_ = web_frame->scrollOffset();
  prev_view_size_ = web_view_->size();

  StartPrinting(webkit_print_params);
}

PrepareFrameAndViewForPrint::~PrepareFrameAndViewForPrint() {
  FinishPrinting();
}

void PrepareFrameAndViewForPrint::UpdatePrintParams(
    const PrintMsg_Print_Params& print_params) {
  DCHECK(!finished_);
  WebKit::WebPrintParams webkit_print_params;
  ComputeWebKitPrintParamsInDesiredDpi(print_params, &webkit_print_params);

  if (webkit_print_params.printContentArea ==
          web_print_params_.printContentArea &&
      webkit_print_params.printableArea == web_print_params_.printableArea &&
      webkit_print_params.paperSize == web_print_params_.paperSize &&
      webkit_print_params.printScalingOption ==
          web_print_params_.printScalingOption) {
    return;
  }

  frame_->printEnd();
  StartPrinting(webkit_print_params);
}

gfx::Size PrepareFrameAndViewForPrint::GetPrintCanvasSize() const {
  return gfx::Size(web_print_params_.printContentArea.width,
                   web_print_params_.printContentArea.height);
}

void PrepareFrameAndViewForPrint::StartPrinting(
    const WebKit::WebPrintParams& webkit_print_params) {
  web_print_params_ = webkit_print_params;

  // Layout page according to printer page size. Since WebKit shrinks the
  // size of the page automatically (from 125% to 200%) we trick it to
  // think the page is 125% larger so the size of the page is correct for
  // minimum (default) scaling.
  // This is important for sites that try to fill the page.
  gfx::Size print_layout_size(web_print_params_.printContentArea.width,
                              web_print_params_.printContentArea.height);
  print_layout_size.set_height(static_cast<int>(
      static_cast<double>(print_layout_size.height()) * 1.25));

  web_view_->resize(print_layout_size);

  expected_pages_count_ = frame_->printBegin(web_print_params_,
                                             node_to_print_,
                                             &use_browser_overlays_);
}

void PrepareFrameAndViewForPrint::FinishPrinting() {
  if (!finished_) {
    finished_ = true;
    frame_->printEnd();
    web_view_->resize(prev_view_size_);
    if (WebKit::WebFrame* web_frame = web_view_->mainFrame())
      web_frame->setScrollOffset(prev_scroll_offset_);
  }
}

PrintWebViewHelper::PrintWebViewHelper(content::RenderView* render_view)
    : content::RenderViewObserver(render_view),
      content::RenderViewObserverTracker<PrintWebViewHelper>(render_view),
      print_web_view_(NULL),
      is_preview_enabled_(IsPrintPreviewEnabled()),
      is_scripted_print_throttling_disabled_(IsPrintThrottlingDisabled()),
      is_print_ready_metafile_sent_(false),
      ignore_css_margins_(false),
      user_cancelled_scripted_print_count_(0),
      is_scripted_printing_blocked_(false),
      notify_browser_of_print_failure_(true),
      print_for_preview_(false),
      print_node_in_progress_(false) {
}

PrintWebViewHelper::~PrintWebViewHelper() {}

bool PrintWebViewHelper::IsScriptInitiatedPrintAllowed(
    WebKit::WebFrame* frame, bool user_initiated) {
  if (is_scripted_printing_blocked_)
    return false;
  // If preview is enabled, then the print dialog is tab modal, and the user
  // can always close the tab on a mis-behaving page (the system print dialog
  // is app modal). If the print was initiated through user action, don't
  // throttle. Or, if the command line flag to skip throttling has been set.
  if (!is_scripted_print_throttling_disabled_ &&
      !is_preview_enabled_ &&
      !user_initiated)
    return !IsScriptInitiatedPrintTooFrequent(frame);
  return true;
}

// Prints |frame| which called window.print().
void PrintWebViewHelper::PrintPage(WebKit::WebFrame* frame,
                                   bool user_initiated) {
  DCHECK(frame);

  // Allow Prerendering to cancel this print request if necessary.
  if (prerender::PrerenderHelper::IsPrerendering(render_view())) {
    Send(new ChromeViewHostMsg_CancelPrerenderForPrinting(routing_id()));
    return;
  }

  if (!IsScriptInitiatedPrintAllowed(frame, user_initiated))
    return;
  IncrementScriptedPrintCount();

  if (is_preview_enabled_) {
    print_preview_context_.InitWithFrame(frame);
    RequestPrintPreview(PRINT_PREVIEW_SCRIPTED);
  } else {
    Print(frame, WebKit::WebNode());
  }
}

bool PrintWebViewHelper::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(PrintWebViewHelper, message)
    IPC_MESSAGE_HANDLER(PrintMsg_PrintPages, OnPrintPages)
    IPC_MESSAGE_HANDLER(PrintMsg_PrintForSystemDialog, OnPrintForSystemDialog)
    IPC_MESSAGE_HANDLER(PrintMsg_InitiatePrintPreview, OnInitiatePrintPreview)
    IPC_MESSAGE_HANDLER(PrintMsg_PrintNodeUnderContextMenu,
                        OnPrintNodeUnderContextMenu)
    IPC_MESSAGE_HANDLER(PrintMsg_PrintPreview, OnPrintPreview)
    IPC_MESSAGE_HANDLER(PrintMsg_PrintForPrintPreview, OnPrintForPrintPreview)
    IPC_MESSAGE_HANDLER(PrintMsg_PrintingDone, OnPrintingDone)
    IPC_MESSAGE_HANDLER(PrintMsg_ResetScriptedPrintCount,
                        ResetScriptedPrintCount)
    IPC_MESSAGE_HANDLER(PrintMsg_SetScriptedPrintingBlocked,
                        SetScriptedPrintBlocked)
    IPC_MESSAGE_UNHANDLED(handled = false)
    IPC_END_MESSAGE_MAP()
  return handled;
}

void PrintWebViewHelper::OnPrintForPrintPreview(
    const DictionaryValue& job_settings) {
  DCHECK(is_preview_enabled_);
  // If still not finished with earlier print request simply ignore.
  if (print_web_view_)
    return;

  if (!render_view()->GetWebView())
    return;
  WebKit::WebFrame* main_frame = render_view()->GetWebView()->mainFrame();
  if (!main_frame)
    return;

  WebKit::WebDocument document = main_frame->document();
  // <object> with id="pdf-viewer" is created in
  // chrome/browser/resources/print_preview/print_preview.js
  WebKit::WebElement pdf_element = document.getElementById("pdf-viewer");
  if (pdf_element.isNull()) {
    NOTREACHED();
    return;
  }

  // Set |print_for_preview_| flag and autoreset it to back to original
  // on return.
  base::AutoReset<bool> set_printing_flag(&print_for_preview_, true);

  WebKit::WebFrame* pdf_frame = pdf_element.document().frame();
  if (!UpdatePrintSettings(pdf_frame, pdf_element, job_settings)) {
    LOG(ERROR) << "UpdatePrintSettings failed";
    DidFinishPrinting(FAIL_PRINT);
    return;
  }

  // Print page onto entire page not just printable area. Preview PDF already
  // has content in correct position taking into account page size and printable
  // area.
  // TODO(vitalybuka) : Make this consistent on all platform. This change
  // affects Windows only. On Linux and OSX RenderPagesForPrint does not use
  // printable_area. Also we can't change printable_area deeper inside
  // RenderPagesForPrint for Windows, because it's used also by native
  // printing and it expects real printable_area value.
  // See http://crbug.com/123408
  PrintMsg_Print_Params& print_params = print_pages_params_->params;
  print_params.printable_area = gfx::Rect(print_params.page_size);

  // Render Pages for printing.
  if (!RenderPagesForPrint(pdf_frame, pdf_element)) {
    LOG(ERROR) << "RenderPagesForPrint failed";
    DidFinishPrinting(FAIL_PRINT);
  }
}

bool PrintWebViewHelper::GetPrintFrame(WebKit::WebFrame** frame) {
  DCHECK(frame);
  DCHECK(render_view()->GetWebView());
  if (!render_view()->GetWebView())
    return false;

  // If the user has selected text in the currently focused frame we print
  // only that frame (this makes print selection work for multiple frames).
  *frame = render_view()->GetWebView()->focusedFrame()->hasSelection() ?
      render_view()->GetWebView()->focusedFrame() :
      render_view()->GetWebView()->mainFrame();
  return true;
}

void PrintWebViewHelper::OnPrintPages() {
  WebKit::WebFrame* frame;
  if (GetPrintFrame(&frame))
    Print(frame, WebKit::WebNode());
}

void PrintWebViewHelper::OnPrintForSystemDialog() {
  WebKit::WebFrame* frame = print_preview_context_.frame();
  if (!frame) {
    NOTREACHED();
    return;
  }

  Print(frame, print_preview_context_.node());
}

void PrintWebViewHelper::GetPageSizeAndContentAreaFromPageLayout(
    const printing::PageSizeMargins& page_layout_in_points,
    gfx::Size* page_size,
    gfx::Rect* content_area) {
  *page_size = gfx::Size(
      page_layout_in_points.content_width +
          page_layout_in_points.margin_right +
          page_layout_in_points.margin_left,
      page_layout_in_points.content_height +
          page_layout_in_points.margin_top +
          page_layout_in_points.margin_bottom);
  *content_area = gfx::Rect(page_layout_in_points.margin_left,
                            page_layout_in_points.margin_top,
                            page_layout_in_points.content_width,
                            page_layout_in_points.content_height);
}

void PrintWebViewHelper::UpdateFrameMarginsCssInfo(
    const DictionaryValue& settings) {
  int margins_type = 0;
  if (!settings.GetInteger(printing::kSettingMarginsType, &margins_type))
    margins_type = printing::DEFAULT_MARGINS;
  ignore_css_margins_ = (margins_type != printing::DEFAULT_MARGINS);
}

bool PrintWebViewHelper::IsPrintToPdfRequested(
    const DictionaryValue& job_settings) {
  bool print_to_pdf = false;
  if (!job_settings.GetBoolean(printing::kSettingPrintToPDF, &print_to_pdf))
    NOTREACHED();
  return print_to_pdf;
}

WebKit::WebPrintScalingOption PrintWebViewHelper::GetPrintScalingOption(
    bool source_is_html, const DictionaryValue& job_settings,
    const PrintMsg_Print_Params& params) {
  DCHECK(!print_for_preview_);

  if (params.print_to_pdf)
    return WebKit::WebPrintScalingOptionSourceSize;

  if (!source_is_html) {
    if (!FitToPageEnabled(job_settings))
      return WebKit::WebPrintScalingOptionNone;

    bool no_plugin_scaling =
        print_preview_context_.frame()->isPrintScalingDisabledForPlugin(
            print_preview_context_.node());

    if (params.is_first_request && no_plugin_scaling)
      return WebKit::WebPrintScalingOptionNone;
  }
  return WebKit::WebPrintScalingOptionFitToPrintableArea;
}

void PrintWebViewHelper::OnPrintPreview(const DictionaryValue& settings) {
  DCHECK(is_preview_enabled_);
  print_preview_context_.OnPrintPreview();

  if (!UpdatePrintSettings(print_preview_context_.frame(),
                           print_preview_context_.node(), settings)) {
    if (print_preview_context_.last_error() != PREVIEW_ERROR_BAD_SETTING) {
      Send(new PrintHostMsg_PrintPreviewInvalidPrinterSettings(
          routing_id(), print_pages_params_->params.document_cookie));
      notify_browser_of_print_failure_ = false;  // Already sent.
    }
    DidFinishPrinting(FAIL_PREVIEW);
    return;
  }

  if (!print_pages_params_->params.is_first_request &&
      old_print_pages_params_.get() &&
      PrintMsg_Print_Params_IsEqual(*old_print_pages_params_,
                                    *print_pages_params_)) {
    PrintHostMsg_DidPreviewDocument_Params preview_params;
    preview_params.reuse_existing_data = true;
    preview_params.data_size = 0;
    preview_params.document_cookie =
        print_pages_params_->params.document_cookie;
    preview_params.expected_pages_count =
        print_preview_context_.total_page_count();
    preview_params.modifiable = print_preview_context_.IsModifiable();
    preview_params.preview_request_id =
        print_pages_params_->params.preview_request_id;

    Send(new PrintHostMsg_MetafileReadyForPrinting(routing_id(),
                                                   preview_params));
    return;
  }

  // If we are previewing a pdf and the print scaling is disabled, send a
  // message to browser.
  if (print_pages_params_->params.is_first_request &&
      !print_preview_context_.IsModifiable() &&
      print_preview_context_.frame()->isPrintScalingDisabledForPlugin(
          print_preview_context_.node())) {
    Send(new PrintHostMsg_PrintPreviewScalingDisabled(routing_id()));
  }

  // Always clear |old_print_pages_params_| before rendering the pages.
  old_print_pages_params_.reset();
  is_print_ready_metafile_sent_ = false;

  // PDF printer device supports alpha blending.
  print_pages_params_->params.supports_alpha_blend = true;

  bool generate_draft_pages = false;
  if (!settings.GetBoolean(printing::kSettingGenerateDraftData,
                           &generate_draft_pages)) {
    NOTREACHED();
  }
  print_preview_context_.set_generate_draft_pages(generate_draft_pages);

  if (CreatePreviewDocument()) {
    DidFinishPrinting(OK);
  } else {
    if (notify_browser_of_print_failure_)
      LOG(ERROR) << "CreatePreviewDocument failed";
    DidFinishPrinting(FAIL_PREVIEW);
  }
}

bool PrintWebViewHelper::CreatePreviewDocument() {
  const PrintMsg_Print_Params& print_params = print_pages_params_->params;
  const std::vector<int>& pages = print_pages_params_->pages;
  if (!print_preview_context_.CreatePreviewDocument(print_params, pages,
                                                    ignore_css_margins_)) {
    return false;
  }

  printing::PageSizeMargins default_page_layout;
  ComputePageLayoutInPointsForCss(print_preview_context_.frame(), 0,
                                  print_params, ignore_css_margins_, NULL,
                                  &default_page_layout);

  if (!old_print_pages_params_.get() ||
      !PageLayoutIsEqual(*old_print_pages_params_, *print_pages_params_)) {
    bool has_page_size_style = PrintingFrameHasPageSizeStyle(
        print_preview_context_.frame(),
        print_preview_context_.total_page_count());
    int dpi = GetDPI(&print_params);

    using printing::ConvertUnit;
    using printing::kPointsPerInch;
    gfx::Rect printable_area_in_points(
        ConvertUnit(print_params.printable_area.x(), dpi, kPointsPerInch),
        ConvertUnit(print_params.printable_area.y(), dpi, kPointsPerInch),
        ConvertUnit(print_params.printable_area.width(), dpi, kPointsPerInch),
        ConvertUnit(print_params.printable_area.height(), dpi, kPointsPerInch));

    // Margins: Send default page layout to browser process.
    Send(new PrintHostMsg_DidGetDefaultPageLayout(routing_id(),
                                                  default_page_layout,
                                                  printable_area_in_points,
                                                  has_page_size_style));
  }

  PrintHostMsg_DidGetPreviewPageCount_Params params;
  params.page_count = print_preview_context_.total_page_count();
  params.is_modifiable = print_preview_context_.IsModifiable();
  params.document_cookie = print_params.document_cookie;
  params.preview_request_id = print_params.preview_request_id;
  params.clear_preview_data = print_preview_context_.generate_draft_pages();
  Send(new PrintHostMsg_DidGetPreviewPageCount(routing_id(), params));
  if (CheckForCancel())
    return false;

  while (!print_preview_context_.IsFinalPageRendered()) {
    int page_number = print_preview_context_.GetNextPageNumber();
    DCHECK_GE(page_number, 0);
    if (!RenderPreviewPage(page_number, print_params))
      return false;

    if (CheckForCancel())
      return false;

    // We must call PrepareFrameAndViewForPrint::FinishPrinting() (by way of
    // print_preview_context_.AllPagesRendered()) before calling
    // FinalizePrintReadyDocument() when printing a PDF because the plugin
    // code does not generate output until we call FinishPrinting().  We do not
    // generate draft pages for PDFs, so IsFinalPageRendered() and
    // IsLastPageOfPrintReadyMetafile() will be true in the same iteration of
    // the loop.
    if (print_preview_context_.IsFinalPageRendered())
      print_preview_context_.AllPagesRendered();

    if (print_preview_context_.IsLastPageOfPrintReadyMetafile()) {
      DCHECK(print_preview_context_.IsModifiable() ||
             print_preview_context_.IsFinalPageRendered());
      if (!FinalizePrintReadyDocument())
        return false;
    }
  }
  print_preview_context_.Finished();
  return true;
}

bool PrintWebViewHelper::FinalizePrintReadyDocument() {
  DCHECK(!is_print_ready_metafile_sent_);
  print_preview_context_.FinalizePrintReadyDocument();

  // Get the size of the resulting metafile.
  printing::PreviewMetafile* metafile = print_preview_context_.metafile();
  uint32 buf_size = metafile->GetDataSize();
  DCHECK_GT(buf_size, 0u);

  PrintHostMsg_DidPreviewDocument_Params preview_params;
  preview_params.reuse_existing_data = false;
  preview_params.data_size = buf_size;
  preview_params.document_cookie = print_pages_params_->params.document_cookie;
  preview_params.expected_pages_count =
      print_preview_context_.total_page_count();
  preview_params.modifiable = print_preview_context_.IsModifiable();
  preview_params.preview_request_id =
      print_pages_params_->params.preview_request_id;

  // Ask the browser to create the shared memory for us.
  if (!CopyMetafileDataToSharedMem(metafile,
                                   &(preview_params.metafile_data_handle))) {
    LOG(ERROR) << "CopyMetafileDataToSharedMem failed";
    print_preview_context_.set_error(PREVIEW_ERROR_METAFILE_COPY_FAILED);
    return false;
  }
  is_print_ready_metafile_sent_ = true;

  Send(new PrintHostMsg_MetafileReadyForPrinting(routing_id(), preview_params));
  return true;
}

void PrintWebViewHelper::OnPrintingDone(bool success) {
  notify_browser_of_print_failure_ = false;
  if (!success)
    LOG(ERROR) << "Failure in OnPrintingDone";
  DidFinishPrinting(success ? OK : FAIL_PRINT);
}

void PrintWebViewHelper::SetScriptedPrintBlocked(bool blocked) {
  is_scripted_printing_blocked_ = blocked;
}

void PrintWebViewHelper::OnPrintNodeUnderContextMenu() {
  PrintNode(render_view()->GetContextMenuNode());
}

void PrintWebViewHelper::OnInitiatePrintPreview() {
  DCHECK(is_preview_enabled_);
  WebKit::WebFrame* frame;
  if (GetPrintFrame(&frame)) {
    print_preview_context_.InitWithFrame(frame);
    RequestPrintPreview(PRINT_PREVIEW_USER_INITIATED_ENTIRE_FRAME);
  } else {
    // This should not happen. Let's add a CHECK here to see how often this
    // gets hit.
    // TODO(thestig) Remove this later when we have sufficient usage of this
    // code on the M19 stable channel.
    CHECK(false);
  }
}

void PrintWebViewHelper::PrintNode(const WebKit::WebNode& node) {
  if (node.isNull() || !node.document().frame()) {
    // This can occur when the context menu refers to an invalid WebNode.
    // See http://crbug.com/100890#c17 for a repro case.
    return;
  }

  if (print_node_in_progress_) {
    // This can happen as a result of processing sync messages when printing
    // from ppapi plugins. It's a rare case, so its OK to just fail here.
    // See http://crbug.com/159165.
    return;
  }

  print_node_in_progress_ = true;

  // Make a copy of the node, in case RenderView::OnContextMenuClosed resets
  // its |context_menu_node_|.
  if (is_preview_enabled_) {
    print_preview_context_.InitWithNode(node);
    RequestPrintPreview(PRINT_PREVIEW_USER_INITIATED_CONTEXT_NODE);
  } else {
    WebKit::WebNode duplicate_node(node);
    Print(duplicate_node.document().frame(), duplicate_node);
  }

  print_node_in_progress_ = false;
}

void PrintWebViewHelper::Print(WebKit::WebFrame* frame,
                               const WebKit::WebNode& node) {
  // If still not finished with earlier print request simply ignore.
  if (print_web_view_)
    return;

  // Initialize print settings.
  scoped_ptr<PrepareFrameAndViewForPrint> prepare;
  if (!InitPrintSettingsAndPrepareFrame(frame, node, &prepare)) {
    DidFinishPrinting(FAIL_PRINT_INIT);
    return;  // Failed to init print page settings.
  }

  int expected_page_count = 0;
  bool use_browser_overlays = true;

  expected_page_count = prepare->GetExpectedPageCount();
  if (expected_page_count)
    use_browser_overlays = prepare->ShouldUseBrowserOverlays();

  // Release the prepare before going any further, since we are going to
  // show UI and wait for the user.
  prepare.reset();

  // Some full screen plugins can say they don't want to print.
  if (!expected_page_count) {
    DidFinishPrinting(FAIL_PRINT);
    return;
  }

  // Ask the browser to show UI to retrieve the final print settings.
  if (!GetPrintSettingsFromUser(frame, node, expected_page_count,
                                use_browser_overlays)) {
    DidFinishPrinting(OK);  // Release resources and fail silently.
    return;
  }

  // Render Pages for printing.
  if (!RenderPagesForPrint(frame, node)) {
    LOG(ERROR) << "RenderPagesForPrint failed";
    DidFinishPrinting(FAIL_PRINT);
  }
  ResetScriptedPrintCount();
}

void PrintWebViewHelper::DidFinishPrinting(PrintingResult result) {
  bool store_print_pages_params = true;
  switch (result) {
    case OK:
      break;

    case FAIL_PRINT_INIT:
      DCHECK(!notify_browser_of_print_failure_);
      break;

    case FAIL_PRINT:
      if (notify_browser_of_print_failure_ && print_pages_params_.get()) {
        int cookie = print_pages_params_->params.document_cookie;
        Send(new PrintHostMsg_PrintingFailed(routing_id(), cookie));
      }
      break;

    case FAIL_PREVIEW:
      DCHECK(is_preview_enabled_);
      store_print_pages_params = false;
      int cookie = print_pages_params_.get() ?
          print_pages_params_->params.document_cookie : 0;
      if (notify_browser_of_print_failure_)
        Send(new PrintHostMsg_PrintPreviewFailed(routing_id(), cookie));
      else
        Send(new PrintHostMsg_PrintPreviewCancelled(routing_id(), cookie));
      print_preview_context_.Failed(notify_browser_of_print_failure_);
      break;
  }

  if (print_web_view_) {
    print_web_view_->close();
    print_web_view_ = NULL;
  }

  if (store_print_pages_params) {
    old_print_pages_params_.reset(print_pages_params_.release());
  } else {
    print_pages_params_.reset();
    old_print_pages_params_.reset();
  }

  notify_browser_of_print_failure_ = true;
}

bool PrintWebViewHelper::CopyAndPrint(WebKit::WebFrame* web_frame) {
  // Create a new WebView with the same settings as the current display one.
  // Except that we disable javascript (don't want any active content running
  // on the page).
  webkit_glue::WebPreferences prefs = render_view()->GetWebkitPreferences();
  prefs.javascript_enabled = false;
  prefs.java_enabled = false;

  print_web_view_ = WebKit::WebView::create(this);
  prefs.Apply(print_web_view_);
  print_web_view_->initializeMainFrame(this);

  print_pages_params_->pages.clear();  // Print all pages of selection.

  std::string html = web_frame->selectionAsMarkup().utf8();
  std::string url_str = "data:text/html;charset=utf-8,";
  url_str.append(html);
  GURL url(url_str);

  // When loading is done this will call didStopLoading() and that will do the
  // actual printing.
  print_web_view_->mainFrame()->loadRequest(WebKit::WebURLRequest(url));

  return true;
}

#if defined(OS_MACOSX) || defined(OS_WIN)
bool PrintWebViewHelper::PrintPages(WebKit::WebFrame* frame,
                                    const WebKit::WebNode& node) {
  const PrintMsg_PrintPages_Params& params = *print_pages_params_;
  const PrintMsg_Print_Params& print_params = params.params;
  PrepareFrameAndViewForPrint prep_frame_view(print_params, frame, node);
  UpdateFrameAndViewFromCssPageLayout(frame, node, &prep_frame_view,
                                      print_params, ignore_css_margins_);

  int page_count = prep_frame_view.GetExpectedPageCount();
  if (!page_count)
    return false;
  // TODO(vitalybuka): should be page_count or valid pages from params.pages.
  // See http://crbug.com/161576
  Send(new PrintHostMsg_DidGetPrintedPagesCount(routing_id(),
                                                print_params.document_cookie,
                                                page_count));

  const gfx::Size& canvas_size = prep_frame_view.GetPrintCanvasSize();
  PrintMsg_PrintPage_Params page_params;
  page_params.params = print_params;
  if (params.pages.empty()) {
    for (int i = 0; i < page_count; ++i) {
      page_params.page_number = i;
      PrintPageInternal(page_params, canvas_size, frame);
    }
  } else {
    for (size_t i = 0; i < params.pages.size(); ++i) {
      if (params.pages[i] >= page_count)
        break;
      page_params.page_number = params.pages[i];
      PrintPageInternal(page_params, canvas_size, frame);
    }
  }
  return true;
}
#endif  // OS_MACOSX || OS_WIN

void PrintWebViewHelper::didStopLoading() {
  PrintPages(print_web_view_->mainFrame(), WebKit::WebNode());
}

// static - Not anonymous so that platform implementations can use it.
void PrintWebViewHelper::ComputePageLayoutInPointsForCss(
    WebKit::WebFrame* frame,
    int page_index,
    const PrintMsg_Print_Params& page_params,
    bool ignore_css_margins,
    double* scale_factor,
    printing::PageSizeMargins* page_layout_in_points) {
  PrintMsg_Print_Params params = CalculatePrintParamsForCss(
      frame, page_index, page_params, ignore_css_margins,
      page_params.print_scaling_option ==
          WebKit::WebPrintScalingOptionFitToPrintableArea,
      scale_factor);
  CalculatePageLayoutFromPrintParams(params, page_layout_in_points);
}

// static - Not anonymous so that platform implementations can use it.
void PrintWebViewHelper::UpdateFrameAndViewFromCssPageLayout(
    WebKit::WebFrame* frame,
    const WebKit::WebNode& node,
    PrepareFrameAndViewForPrint* prepare,
    const PrintMsg_Print_Params& params,
    bool ignore_css_margins) {
  if (PrintingNodeOrPdfFrame(frame, node))
    return;
  bool fit_to_page = ignore_css_margins &&
                     params.print_scaling_option ==
                          WebKit::WebPrintScalingOptionFitToPrintableArea;
  PrintMsg_Print_Params print_params = CalculatePrintParamsForCss(
      frame, 0, params, ignore_css_margins, fit_to_page, NULL);
  prepare->UpdatePrintParams(print_params);
}

bool PrintWebViewHelper::InitPrintSettings(bool fit_to_paper_size) {
  PrintMsg_PrintPages_Params settings;
  Send(new PrintHostMsg_GetDefaultPrintSettings(routing_id(),
                                                &settings.params));
  // Check if the printer returned any settings, if the settings is empty, we
  // can safely assume there are no printer drivers configured. So we safely
  // terminate.
  bool result = true;
  if (!PrintMsg_Print_Params_IsValid(settings.params))
    result = false;

  if (result &&
      (settings.params.dpi < kMinDpi || settings.params.document_cookie == 0)) {
    // Invalid print page settings.
    NOTREACHED();
    result = false;
  }

  // Reset to default values.
  ignore_css_margins_ = false;
  settings.pages.clear();

  settings.params.print_scaling_option =
      WebKit::WebPrintScalingOptionSourceSize;
  if (fit_to_paper_size) {
    settings.params.print_scaling_option =
        WebKit::WebPrintScalingOptionFitToPrintableArea;
  }

  print_pages_params_.reset(new PrintMsg_PrintPages_Params(settings));
  return result;
}

bool PrintWebViewHelper::InitPrintSettingsAndPrepareFrame(
    WebKit::WebFrame* frame,
    const WebKit::WebNode& node,
    scoped_ptr<PrepareFrameAndViewForPrint>* prepare) {
  DCHECK(frame);

  bool fit_to_paper_size = !(PrintingNodeOrPdfFrame(frame, node));
  if (!InitPrintSettings(fit_to_paper_size)) {
    notify_browser_of_print_failure_ = false;
    render_view()->RunModalAlertDialog(
        frame,
        l10n_util::GetStringUTF16(IDS_PRINT_PREVIEW_INVALID_PRINTER_SETTINGS));
    return false;
  }

  DCHECK(!prepare->get());
  prepare->reset(new PrepareFrameAndViewForPrint(print_pages_params_->params,
                                                 frame, node));
  UpdateFrameAndViewFromCssPageLayout(frame, node, prepare->get(),
                                      print_pages_params_->params,
                                      ignore_css_margins_);
  Send(new PrintHostMsg_DidGetDocumentCookie(
        routing_id(), print_pages_params_->params.document_cookie));
  return true;
}

bool PrintWebViewHelper::UpdatePrintSettings(
    WebKit::WebFrame* frame,
    const WebKit::WebNode& node,
    const DictionaryValue& passed_job_settings) {
  DCHECK(is_preview_enabled_);
  const DictionaryValue* job_settings = &passed_job_settings;
  DictionaryValue modified_job_settings;
  if (job_settings->empty()) {
    if (!print_for_preview_)
      print_preview_context_.set_error(PREVIEW_ERROR_BAD_SETTING);
    return false;
  }

  bool source_is_html = true;
  if (print_for_preview_) {
    if (!job_settings->GetBoolean(printing::kSettingPreviewModifiable,
                                  &source_is_html)) {
      NOTREACHED();
    }
  } else {
    source_is_html = !PrintingNodeOrPdfFrame(frame, node);
  }

  if (print_for_preview_ || !source_is_html) {
    modified_job_settings.MergeDictionary(job_settings);
    modified_job_settings.SetBoolean(printing::kSettingHeaderFooterEnabled,
                                     false);
    modified_job_settings.SetInteger(printing::kSettingMarginsType,
                                     printing::NO_MARGINS);
    job_settings = &modified_job_settings;
  }

  // Send the cookie so that UpdatePrintSettings can reuse PrinterQuery when
  // possible.
  int cookie = print_pages_params_.get() ?
      print_pages_params_->params.document_cookie : 0;
  PrintMsg_PrintPages_Params settings;
  Send(new PrintHostMsg_UpdatePrintSettings(routing_id(),
      cookie, *job_settings, &settings));
  print_pages_params_.reset(new PrintMsg_PrintPages_Params(settings));

  if (!PrintMsg_Print_Params_IsValid(settings.params)) {
    if (!print_for_preview_) {
      print_preview_context_.set_error(PREVIEW_ERROR_INVALID_PRINTER_SETTINGS);
    } else {
      // PrintForPrintPreview
      WebKit::WebFrame* print_frame = NULL;
      // This may not be the right frame, but the alert will be modal,
      // therefore it works well enough.
      GetPrintFrame(&print_frame);
      if (print_frame) {
        render_view()->RunModalAlertDialog(
            print_frame,
            l10n_util::GetStringUTF16(
                IDS_PRINT_PREVIEW_INVALID_PRINTER_SETTINGS));
      }
    }
    return false;
  }

  if (settings.params.dpi < kMinDpi || !settings.params.document_cookie) {
    print_preview_context_.set_error(PREVIEW_ERROR_UPDATING_PRINT_SETTINGS);
    return false;
  }

  if (!print_for_preview_) {
    // Validate expected print preview settings.
    if (!job_settings->GetInteger(printing::kPreviewUIID,
                                  &(settings.params.preview_ui_id)) ||
        !job_settings->GetInteger(printing::kPreviewRequestID,
                                  &(settings.params.preview_request_id)) ||
        !job_settings->GetBoolean(printing::kIsFirstRequest,
                                  &(settings.params.is_first_request))) {
      NOTREACHED();
      print_preview_context_.set_error(PREVIEW_ERROR_BAD_SETTING);
      return false;
    }

    settings.params.print_to_pdf = IsPrintToPdfRequested(*job_settings);
    UpdateFrameMarginsCssInfo(*job_settings);
    settings.params.print_scaling_option = GetPrintScalingOption(
        source_is_html, *job_settings, settings.params);

    // Header/Footer: Set |header_footer_info_|.
    if (settings.params.display_header_footer) {
      header_footer_info_.reset(new DictionaryValue());
      header_footer_info_->SetString(printing::kSettingHeaderFooterDate,
                                     settings.params.date);
      header_footer_info_->SetString(printing::kSettingHeaderFooterURL,
                                     settings.params.url);
      header_footer_info_->SetString(printing::kSettingHeaderFooterTitle,
                                     settings.params.title);
    }
  }

  print_pages_params_.reset(new PrintMsg_PrintPages_Params(settings));
  Send(new PrintHostMsg_DidGetDocumentCookie(routing_id(),
                                             settings.params.document_cookie));
  return true;
}

bool PrintWebViewHelper::GetPrintSettingsFromUser(WebKit::WebFrame* frame,
                                                  const WebKit::WebNode& node,
                                                  int expected_pages_count,
                                                  bool use_browser_overlays) {
  PrintHostMsg_ScriptedPrint_Params params;
  PrintMsg_PrintPages_Params print_settings;

  params.cookie = print_pages_params_->params.document_cookie;
  params.has_selection = frame->hasSelection();
  params.expected_pages_count = expected_pages_count;
  printing::MarginType margin_type = printing::DEFAULT_MARGINS;
  if (PrintingNodeOrPdfFrame(frame, node))
    margin_type = GetMarginsForPdf(frame, node);
  params.margin_type = margin_type;

  Send(new PrintHostMsg_DidShowPrintDialog(routing_id()));

  // PrintHostMsg_ScriptedPrint will reset print_scaling_option, so we save the
  // value before and restore it afterwards.
  WebKit::WebPrintScalingOption scaling_option =
      print_pages_params_->params.print_scaling_option;

  print_pages_params_.reset();
  IPC::SyncMessage* msg =
      new PrintHostMsg_ScriptedPrint(routing_id(), params, &print_settings);
  msg->EnableMessagePumping();
  Send(msg);
  print_pages_params_.reset(new PrintMsg_PrintPages_Params(print_settings));

  print_pages_params_->params.print_scaling_option = scaling_option;
  return (print_settings.params.dpi && print_settings.params.document_cookie);
}

bool PrintWebViewHelper::RenderPagesForPrint(
    WebKit::WebFrame* frame,
    const WebKit::WebNode& node) {
  if (print_pages_params_->params.selection_only)
    return CopyAndPrint(frame);
  return PrintPages(frame, node);
}

#if defined(OS_POSIX)
bool PrintWebViewHelper::CopyMetafileDataToSharedMem(
    printing::Metafile* metafile,
    base::SharedMemoryHandle* shared_mem_handle) {
  uint32 buf_size = metafile->GetDataSize();
  base::SharedMemoryHandle mem_handle =
      content::RenderThread::Get()->HostAllocateSharedMemoryBuffer(buf_size);
  if (base::SharedMemory::IsHandleValid(mem_handle)) {
    base::SharedMemory shared_buf(mem_handle, false);
    if (shared_buf.Map(buf_size)) {
      metafile->GetData(shared_buf.memory(), buf_size);
      shared_buf.GiveToProcess(base::GetCurrentProcessHandle(),
                               shared_mem_handle);
      return true;
    }
  }
  NOTREACHED();
  return false;
}
#endif  // defined(OS_POSIX)

bool PrintWebViewHelper::IsScriptInitiatedPrintTooFrequent(
    WebKit::WebFrame* frame) {
  const int kMinSecondsToIgnoreJavascriptInitiatedPrint = 2;
  const int kMaxSecondsToIgnoreJavascriptInitiatedPrint = 32;
  bool too_frequent = false;

  // Check if there is script repeatedly trying to print and ignore it if too
  // frequent.  The first 3 times, we use a constant wait time, but if this
  // gets excessive, we switch to exponential wait time. So for a page that
  // calls print() in a loop the user will need to cancel the print dialog
  // after: [2, 2, 2, 4, 8, 16, 32, 32, ...] seconds.
  // This gives the user time to navigate from the page.
  if (user_cancelled_scripted_print_count_ > 0) {
    base::TimeDelta diff = base::Time::Now() - last_cancelled_script_print_;
    int min_wait_seconds = kMinSecondsToIgnoreJavascriptInitiatedPrint;
    if (user_cancelled_scripted_print_count_ > 3) {
      min_wait_seconds = std::min(
          kMinSecondsToIgnoreJavascriptInitiatedPrint <<
              (user_cancelled_scripted_print_count_ - 3),
          kMaxSecondsToIgnoreJavascriptInitiatedPrint);
    }
    if (diff.InSeconds() < min_wait_seconds) {
      too_frequent = true;
    }
  }

  if (!too_frequent)
    return false;

  WebKit::WebString message(
      WebKit::WebString::fromUTF8("Ignoring too frequent calls to print()."));
  frame->addMessageToConsole(
      WebKit::WebConsoleMessage(
          WebKit::WebConsoleMessage::LevelWarning, message));
  return true;
}

void PrintWebViewHelper::ResetScriptedPrintCount() {
  // Reset cancel counter on successful print.
  user_cancelled_scripted_print_count_ = 0;
}

void PrintWebViewHelper::IncrementScriptedPrintCount() {
  ++user_cancelled_scripted_print_count_;
  last_cancelled_script_print_ = base::Time::Now();
}

void PrintWebViewHelper::RequestPrintPreview(PrintPreviewRequestType type) {
  const bool is_modifiable = print_preview_context_.IsModifiable();
  old_print_pages_params_.reset();
  switch (type) {
    case PRINT_PREVIEW_USER_INITIATED_ENTIRE_FRAME: {
      Send(new PrintHostMsg_RequestPrintPreview(routing_id(), is_modifiable,
                                                false));
      break;
    }
    case PRINT_PREVIEW_USER_INITIATED_CONTEXT_NODE: {
      Send(new PrintHostMsg_RequestPrintPreview(routing_id(), is_modifiable,
                                                true));
      break;
    }
    case PRINT_PREVIEW_SCRIPTED: {
      IPC::SyncMessage* msg =
          new PrintHostMsg_ScriptedPrintPreview(routing_id(), is_modifiable);
      msg->EnableMessagePumping();
      Send(msg);
      break;
    }
    default: {
      NOTREACHED();
      return;
    }
  }
}

bool PrintWebViewHelper::CheckForCancel() {
  const PrintMsg_Print_Params& print_params = print_pages_params_->params;
  bool cancel = false;
  Send(new PrintHostMsg_CheckForCancel(routing_id(),
                                       print_params.preview_ui_id,
                                       print_params.preview_request_id,
                                       &cancel));
  if (cancel)
    notify_browser_of_print_failure_ = false;
  return cancel;
}

bool PrintWebViewHelper::PreviewPageRendered(int page_number,
                                             printing::Metafile* metafile) {
  DCHECK_GE(page_number, printing::FIRST_PAGE_INDEX);

  // For non-modifiable files, |metafile| should be NULL, so do not bother
  // sending a message. If we don't generate draft metafiles, |metafile| is
  // NULL.
  if (!print_preview_context_.IsModifiable() ||
      !print_preview_context_.generate_draft_pages()) {
    DCHECK(!metafile);
    return true;
  }

  if (!metafile) {
    NOTREACHED();
    print_preview_context_.set_error(
        PREVIEW_ERROR_PAGE_RENDERED_WITHOUT_METAFILE);
    return false;
  }

  PrintHostMsg_DidPreviewPage_Params preview_page_params;
  // Get the size of the resulting metafile.
  uint32 buf_size = metafile->GetDataSize();
  DCHECK_GT(buf_size, 0u);
  if (!CopyMetafileDataToSharedMem(
      metafile, &(preview_page_params.metafile_data_handle))) {
    LOG(ERROR) << "CopyMetafileDataToSharedMem failed";
    print_preview_context_.set_error(PREVIEW_ERROR_METAFILE_COPY_FAILED);
    return false;
  }
  preview_page_params.data_size = buf_size;
  preview_page_params.page_number = page_number;
  preview_page_params.preview_request_id =
      print_pages_params_->params.preview_request_id;

  Send(new PrintHostMsg_DidPreviewPage(routing_id(), preview_page_params));
  return true;
}

PrintWebViewHelper::PrintPreviewContext::PrintPreviewContext()
    : frame_(NULL),
      total_page_count_(0),
      current_page_index_(0),
      generate_draft_pages_(true),
      print_ready_metafile_page_count_(0),
      error_(PREVIEW_ERROR_NONE),
      state_(UNINITIALIZED) {
}

PrintWebViewHelper::PrintPreviewContext::~PrintPreviewContext() {
}

void PrintWebViewHelper::PrintPreviewContext::InitWithFrame(
    WebKit::WebFrame* web_frame) {
  DCHECK(web_frame);
  DCHECK(!IsRendering());
  state_ = INITIALIZED;
  frame_ = web_frame;
  node_.reset();
}

void PrintWebViewHelper::PrintPreviewContext::InitWithNode(
    const WebKit::WebNode& web_node) {
  DCHECK(!web_node.isNull());
  DCHECK(web_node.document().frame());
  DCHECK(!IsRendering());
  state_ = INITIALIZED;
  frame_ = web_node.document().frame();
  node_ = web_node;
}

void PrintWebViewHelper::PrintPreviewContext::OnPrintPreview() {
  DCHECK_EQ(INITIALIZED, state_);
  ClearContext();
}

bool PrintWebViewHelper::PrintPreviewContext::CreatePreviewDocument(
    const PrintMsg_Print_Params& print_params,
    const std::vector<int>& pages,
    bool ignore_css_margins) {
  DCHECK_EQ(INITIALIZED, state_);
  state_ = RENDERING;

  metafile_.reset(new printing::PreviewMetafile);
  if (!metafile_->Init()) {
    set_error(PREVIEW_ERROR_METAFILE_INIT_FAILED);
    LOG(ERROR) << "PreviewMetafile Init failed";
    return false;
  }

  // Need to make sure old object gets destroyed first.
  prep_frame_view_.reset(new PrepareFrameAndViewForPrint(print_params, frame(),
                                                         node()));
  UpdateFrameAndViewFromCssPageLayout(frame_, node_, prep_frame_view_.get(),
                                      print_params, ignore_css_margins);

  total_page_count_ = prep_frame_view_->GetExpectedPageCount();
  if (total_page_count_ == 0) {
    LOG(ERROR) << "CreatePreviewDocument got 0 page count";
    set_error(PREVIEW_ERROR_ZERO_PAGES);
    return false;
  }

  int selected_page_count = pages.size();
  current_page_index_ = 0;
  print_ready_metafile_page_count_ = selected_page_count;
  pages_to_render_ = pages;

  if (selected_page_count == 0) {
    print_ready_metafile_page_count_ = total_page_count_;
    // Render all pages.
    for (int i = 0; i < total_page_count_; ++i)
      pages_to_render_.push_back(i);
  } else if (generate_draft_pages_) {
    int pages_index = 0;
    for (int i = 0; i < total_page_count_; ++i) {
      if (pages_index < selected_page_count && i == pages[pages_index]) {
        pages_index++;
        continue;
      }
      pages_to_render_.push_back(i);
    }
  }

  document_render_time_ = base::TimeDelta();
  begin_time_ = base::TimeTicks::Now();

  return true;
}

void PrintWebViewHelper::PrintPreviewContext::RenderedPreviewPage(
    const base::TimeDelta& page_time) {
  DCHECK_EQ(RENDERING, state_);
  document_render_time_ += page_time;
  UMA_HISTOGRAM_TIMES("PrintPreview.RenderPDFPageTime", page_time);
}

void PrintWebViewHelper::PrintPreviewContext::AllPagesRendered() {
  DCHECK_EQ(RENDERING, state_);
  state_ = DONE;
  prep_frame_view_->FinishPrinting();
}

void PrintWebViewHelper::PrintPreviewContext::FinalizePrintReadyDocument() {
  DCHECK(IsRendering());

  base::TimeTicks begin_time = base::TimeTicks::Now();
  metafile_->FinishDocument();

  if (print_ready_metafile_page_count_ <= 0) {
    NOTREACHED();
    return;
  }

  UMA_HISTOGRAM_MEDIUM_TIMES("PrintPreview.RenderToPDFTime",
                             document_render_time_);
  base::TimeDelta total_time = (base::TimeTicks::Now() - begin_time) +
                               document_render_time_;
  UMA_HISTOGRAM_MEDIUM_TIMES("PrintPreview.RenderAndGeneratePDFTime",
                             total_time);
  UMA_HISTOGRAM_MEDIUM_TIMES("PrintPreview.RenderAndGeneratePDFTimeAvgPerPage",
                             total_time / pages_to_render_.size());
}

void PrintWebViewHelper::PrintPreviewContext::Finished() {
  DCHECK_EQ(DONE, state_);
  state_ = INITIALIZED;
  ClearContext();
}

void PrintWebViewHelper::PrintPreviewContext::Failed(bool report_error) {
  DCHECK(state_ == INITIALIZED || state_ == RENDERING);
  state_ = INITIALIZED;
  if (report_error) {
    DCHECK_NE(PREVIEW_ERROR_NONE, error_);
    UMA_HISTOGRAM_ENUMERATION("PrintPreview.RendererError", error_,
                              PREVIEW_ERROR_LAST_ENUM);
  }
  ClearContext();
}

int PrintWebViewHelper::PrintPreviewContext::GetNextPageNumber() {
  DCHECK_EQ(RENDERING, state_);
  if (IsFinalPageRendered())
    return -1;
  return pages_to_render_[current_page_index_++];
}

bool PrintWebViewHelper::PrintPreviewContext::IsRendering() const {
  return state_ == RENDERING || state_ == DONE;
}

bool PrintWebViewHelper::PrintPreviewContext::IsModifiable() const {
  // The only kind of node we can print right now is a PDF node.
  return !PrintingNodeOrPdfFrame(frame_, node_);
}

bool PrintWebViewHelper::PrintPreviewContext::IsLastPageOfPrintReadyMetafile()
    const {
  DCHECK(IsRendering());
  return current_page_index_ == print_ready_metafile_page_count_;
}

bool  PrintWebViewHelper::PrintPreviewContext::IsFinalPageRendered() const {
  DCHECK(IsRendering());
  return static_cast<size_t>(current_page_index_) == pages_to_render_.size();
}

void PrintWebViewHelper::PrintPreviewContext::set_generate_draft_pages(
    bool generate_draft_pages) {
  DCHECK_EQ(INITIALIZED, state_);
  generate_draft_pages_ = generate_draft_pages;
}

void PrintWebViewHelper::PrintPreviewContext::set_error(
    enum PrintPreviewErrorBuckets error) {
  error_ = error;
}

WebKit::WebFrame* PrintWebViewHelper::PrintPreviewContext::frame() {
  // TODO(thestig) turn this back into a DCHECK when http://crbug.com/118303 is
  // resolved.
  CHECK(state_ != UNINITIALIZED);
  return frame_;
}

const WebKit::WebNode& PrintWebViewHelper::PrintPreviewContext::node() const {
  DCHECK(state_ != UNINITIALIZED);
  return node_;
}

int PrintWebViewHelper::PrintPreviewContext::total_page_count() const {
  DCHECK(state_ != UNINITIALIZED);
  return total_page_count_;
}

bool PrintWebViewHelper::PrintPreviewContext::generate_draft_pages() const {
  return generate_draft_pages_;
}

printing::PreviewMetafile* PrintWebViewHelper::PrintPreviewContext::metafile() {
  DCHECK(IsRendering());
  return metafile_.get();
}

int PrintWebViewHelper::PrintPreviewContext::last_error() const {
  return error_;
}

gfx::Size PrintWebViewHelper::PrintPreviewContext::GetPrintCanvasSize() const {
  DCHECK(IsRendering());
  return prep_frame_view_->GetPrintCanvasSize();
}

void PrintWebViewHelper::PrintPreviewContext::ClearContext() {
  prep_frame_view_.reset();
  metafile_.reset();
  pages_to_render_.clear();
  error_ = PREVIEW_ERROR_NONE;
}
