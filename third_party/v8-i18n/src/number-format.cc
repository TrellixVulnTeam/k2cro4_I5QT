// Copyright 2012 the v8-i18n authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/number-format.h"

#include <string.h>

#include "src/utils.h"
#include "unicode/curramt.h"
#include "unicode/dcfmtsym.h"
#include "unicode/decimfmt.h"
#include "unicode/locid.h"
#include "unicode/numfmt.h"
#include "unicode/numsys.h"
#include "unicode/uchar.h"
#include "unicode/ucurr.h"
#include "unicode/unum.h"
#include "unicode/uversion.h"

namespace v8_i18n {

static icu::DecimalFormat* InitializeNumberFormat(v8::Handle<v8::String>,
                                                  v8::Handle<v8::Object>,
                                                  v8::Handle<v8::Object>);
static icu::DecimalFormat* CreateICUNumberFormat(const icu::Locale&,
                                                 v8::Handle<v8::Object>);
static void SetResolvedSettings(const icu::Locale&,
                                icu::DecimalFormat*,
                                v8::Handle<v8::Object>);

icu::DecimalFormat* NumberFormat::UnpackNumberFormat(
    v8::Handle<v8::Object> obj) {
  v8::HandleScope handle_scope;

  // v8::ObjectTemplate doesn't have HasInstance method so we can't check
  // if obj is an instance of NumberFormat class. We'll check for a property
  // that has to be in the object. The same applies to other services, like
  // Collator and DateTimeFormat.
  if (obj->HasOwnProperty(v8::String::New("numberFormat"))) {
    return static_cast<icu::DecimalFormat*>(
        obj->GetAlignedPointerFromInternalField(0));
  }

  return NULL;
}

void NumberFormat::DeleteNumberFormat(v8::Persistent<v8::Value> object,
                                      void* param) {
  v8::Persistent<v8::Object> persistent_object =
      v8::Persistent<v8::Object>::Cast(object);

  // First delete the hidden C++ object.
  // Unpacking should never return NULL here. That would only happen if
  // this method is used as the weak callback for persistent handles not
  // pointing to a date time formatter.
  delete UnpackNumberFormat(persistent_object);

  // Then dispose of the persistent handle to JS object.
  persistent_object.Dispose();
}

v8::Handle<v8::Value> NumberFormat::JSInternalFormat(
    const v8::Arguments& args) {
  v8::HandleScope handle_scope;

  if (args.Length() != 2 || !args[0]->IsObject() || !args[1]->IsNumber()) {
    return v8::ThrowException(v8::Exception::Error(
        v8::String::New("Formatter and numeric value have to be specified.")));
  }

  icu::DecimalFormat* number_format = UnpackNumberFormat(args[0]->ToObject());
  if (!number_format) {
    return v8::ThrowException(v8::Exception::Error(
        v8::String::New("NumberFormat method called on an object "
                        "that is not a NumberFormat.")));
  }

  // ICU will handle actual NaN value properly and return NaN string.
  icu::UnicodeString result;
  number_format->format(args[1]->NumberValue(), result);

  return v8::String::New(
      reinterpret_cast<const uint16_t*>(result.getBuffer()), result.length());
}

v8::Handle<v8::Value> NumberFormat::JSInternalParse(
    const v8::Arguments& args) {
  v8::HandleScope handle_scope;

  if (args.Length() != 2 || !args[0]->IsObject() || !args[1]->IsString()) {
    return v8::ThrowException(v8::Exception::Error(
        v8::String::New("Formatter and string have to be specified.")));
  }

  icu::DecimalFormat* number_format = UnpackNumberFormat(args[0]->ToObject());
  if (!number_format) {
    return v8::ThrowException(v8::Exception::Error(
        v8::String::New("NumberFormat method called on an object "
                        "that is not a NumberFormat.")));
  }

  // ICU will handle actual NaN value properly and return NaN string.
  icu::UnicodeString string_number;
  if (!Utils::V8StringToUnicodeString(args[1]->ToString(), &string_number)) {
    string_number = "";
  }

  UErrorCode status = U_ZERO_ERROR;
  icu::Formattable result;
  // ICU 4.6 doesn't support parseCurrency call. We need to wait for ICU49
  // to be part of Chrome.
  // TODO(cira): Include currency parsing code using parseCurrency call.
  // We need to check if the formatter parses all currencies or only the
  // one it was constructed with (it will impact the API - how to return ISO
  // code and the value).
  number_format->parse(string_number, result, status);
  if (U_FAILURE(status)) {
    return v8::Undefined();
  }

  switch (result.getType()) {
  case icu::Formattable::kDouble:
    return v8::Number::New(result.getDouble());
    break;
  case icu::Formattable::kLong:
    return v8::Number::New(result.getLong());
    break;
  case icu::Formattable::kInt64:
    return v8::Number::New(result.getInt64());
    break;
  default:
    return v8::Undefined();
  }

  // To make compiler happy.
  return v8::Undefined();
}

v8::Handle<v8::Value> NumberFormat::JSCreateNumberFormat(
    const v8::Arguments& args) {
  v8::HandleScope handle_scope;

  if (args.Length() != 3 ||
      !args[0]->IsString() ||
      !args[1]->IsObject() ||
      !args[2]->IsObject()) {
    return v8::ThrowException(v8::Exception::Error(
        v8::String::New("Internal error, wrong parameters.")));
  }

  v8::Persistent<v8::ObjectTemplate> number_format_template =
      Utils::GetTemplate();

  // Create an empty object wrapper.
  v8::Local<v8::Object> local_object = number_format_template->NewInstance();
  // But the handle shouldn't be empty.
  // That can happen if there was a stack overflow when creating the object.
  if (local_object.IsEmpty()) {
    return local_object;
  }

  v8::Persistent<v8::Object> wrapper =
      v8::Persistent<v8::Object>::New(local_object);

  // Set number formatter as internal field of the resulting JS object.
  icu::DecimalFormat* number_format = InitializeNumberFormat(
      args[0]->ToString(), args[1]->ToObject(), args[2]->ToObject());

  if (!number_format) {
    return v8::ThrowException(v8::Exception::Error(v8::String::New(
        "Internal error. Couldn't create ICU number formatter.")));
  } else {
    wrapper->SetAlignedPointerInInternalField(0, number_format);

    v8::TryCatch try_catch;
    wrapper->Set(v8::String::New("numberFormat"), v8::String::New("valid"));
    if (try_catch.HasCaught()) {
      return v8::ThrowException(v8::Exception::Error(
          v8::String::New("Internal error, couldn't set property.")));
    }
  }

  // Make object handle weak so we can delete iterator once GC kicks in.
  wrapper.MakeWeak(NULL, DeleteNumberFormat);

  return wrapper;
}

static icu::DecimalFormat* InitializeNumberFormat(
    v8::Handle<v8::String> locale,
    v8::Handle<v8::Object> options,
    v8::Handle<v8::Object> resolved) {
  v8::HandleScope handle_scope;

  // Convert BCP47 into ICU locale format.
  UErrorCode status = U_ZERO_ERROR;
  icu::Locale icu_locale;
  char icu_result[ULOC_FULLNAME_CAPACITY];
  int icu_length = 0;
  v8::String::AsciiValue bcp47_locale(locale);
  if (bcp47_locale.length() != 0) {
    uloc_forLanguageTag(*bcp47_locale, icu_result, ULOC_FULLNAME_CAPACITY,
                        &icu_length, &status);
    if (U_FAILURE(status) || icu_length == 0) {
      return NULL;
    }
    icu_locale = icu::Locale(icu_result);
  }

  icu::DecimalFormat* number_format =
      CreateICUNumberFormat(icu_locale, options);
  if (!number_format) {
    // Remove extensions and try again.
    icu::Locale no_extension_locale(icu_locale.getBaseName());
    number_format = CreateICUNumberFormat(no_extension_locale, options);

    // Set resolved settings (pattern, numbering system).
    SetResolvedSettings(no_extension_locale, number_format, resolved);
  } else {
    SetResolvedSettings(icu_locale, number_format, resolved);
  }

  return number_format;
}

static icu::DecimalFormat* CreateICUNumberFormat(
    const icu::Locale& icu_locale, v8::Handle<v8::Object> options) {
  // Make formatter from options. Numbering system is added
  // to the locale as Unicode extension (if it was specified at all).
  UErrorCode status = U_ZERO_ERROR;
  icu::DecimalFormat* number_format = NULL;
  icu::UnicodeString style;
  icu::UnicodeString currency;
  if (Utils::ExtractStringSetting(options, "style", &style)) {
    if (style == UNICODE_STRING_SIMPLE("currency")) {
      Utils::ExtractStringSetting(options, "currency", &currency);

      icu::UnicodeString display;
      Utils::ExtractStringSetting(options, "currencyDisplay", &display);
#if (U_ICU_VERSION_MAJOR_NUM == 4) && (U_ICU_VERSION_MINOR_NUM <= 6)
      icu::NumberFormat::EStyles style;
      if (display == UNICODE_STRING_SIMPLE("code")) {
        style = icu::NumberFormat::kIsoCurrencyStyle;
      } else if (display == UNICODE_STRING_SIMPLE("name")) {
        style = icu::NumberFormat::kPluralCurrencyStyle;
      } else {
        style = icu::NumberFormat::kCurrencyStyle;
      }
#else  // ICU version is 4.8 or above (we ignore versions below 4.0).
      UNumberFormatStyle style;
      if (display == UNICODE_STRING_SIMPLE("code")) {
        style = UNUM_CURRENCY_ISO;
      } else if (display == UNICODE_STRING_SIMPLE("name")) {
        style = UNUM_CURRENCY_PLURAL;
      } else {
        style = UNUM_CURRENCY;
      }
#endif

      number_format = static_cast<icu::DecimalFormat*>(
          icu::NumberFormat::createInstance(icu_locale, style,  status));
    } else if (style == UNICODE_STRING_SIMPLE("percent")) {
      number_format = static_cast<icu::DecimalFormat*>(
          icu::NumberFormat::createPercentInstance(icu_locale, status));
      if (U_FAILURE(status)) {
        delete number_format;
        return NULL;
      }
      // Make sure 1.1% doesn't go into 2%.
      number_format->setMinimumFractionDigits(1);
    } else {
      // Make a decimal instance by default.
      number_format = static_cast<icu::DecimalFormat*>(
          icu::NumberFormat::createInstance(icu_locale, status));
    }
  }

  if (U_FAILURE(status)) {
    delete number_format;
    return NULL;
  }

  // Set all options.
  if (!currency.isEmpty()) {
    number_format->setCurrency(currency.getBuffer(), status);
  }

  int32_t digits;
  if (Utils::ExtractIntegerSetting(
          options, "minimumIntegerDigits", &digits)) {
    number_format->setMinimumIntegerDigits(digits);
  }

  if (Utils::ExtractIntegerSetting(
          options, "minimumFractionDigits", &digits)) {
    number_format->setMinimumFractionDigits(digits);
  }

  if (Utils::ExtractIntegerSetting(
          options, "maximumFractionDigits", &digits)) {
    number_format->setMaximumFractionDigits(digits);
  }

  if (Utils::ExtractIntegerSetting(
          options, "minimumSignificantDigits", &digits)) {
    number_format->setMinimumSignificantDigits(digits);
  }

  if (Utils::ExtractIntegerSetting(
          options, "maximumSignificantDigits", &digits)) {
    number_format->setMaximumSignificantDigits(digits);
  }

  bool grouping;
  if (Utils::ExtractBooleanSetting(options, "useGrouping", &grouping)) {
    number_format->setGroupingUsed(grouping);
  }

  // Set rounding mode.
  number_format->setRoundingMode(icu::DecimalFormat::kRoundHalfUp);

  return number_format;
}

static void SetResolvedSettings(const icu::Locale& icu_locale,
                                icu::DecimalFormat* number_format,
                                v8::Handle<v8::Object> resolved) {
  v8::HandleScope handle_scope;

  icu::UnicodeString pattern;
  number_format->toPattern(pattern);
  resolved->Set(v8::String::New("pattern"),
                v8::String::New(reinterpret_cast<const uint16_t*>(
                    pattern.getBuffer()), pattern.length()));

  // Set resolved currency code in options.currency if not empty.
  icu::UnicodeString currency(number_format->getCurrency());
  if (!currency.isEmpty()) {
    resolved->Set(v8::String::New("currency"),
                  v8::String::New(reinterpret_cast<const uint16_t*>(
                      currency.getBuffer()), currency.length()));
  }

  // Ugly hack. ICU doesn't expose numbering system in any way, so we have
  // to assume that for given locale NumberingSystem constructor produces the
  // same digits as NumberFormat would.
  UErrorCode status = U_ZERO_ERROR;
  icu::NumberingSystem* numbering_system =
      icu::NumberingSystem::createInstance(icu_locale, status);
  if (U_SUCCESS(status)) {
    const char* ns = numbering_system->getName();
    resolved->Set(v8::String::New("numberingSystem"), v8::String::New(ns));
  } else {
    resolved->Set(v8::String::New("numberingSystem"), v8::Undefined());
  }
  delete numbering_system;

  resolved->Set(v8::String::New("useGrouping"),
                v8::Boolean::New(number_format->isGroupingUsed()));

  resolved->Set(v8::String::New("minimumIntegerDigits"),
                v8::Integer::New(number_format->getMinimumIntegerDigits()));

  resolved->Set(v8::String::New("minimumFractionDigits"),
                v8::Integer::New(number_format->getMinimumFractionDigits()));

  resolved->Set(v8::String::New("maximumFractionDigits"),
                v8::Integer::New(number_format->getMaximumFractionDigits()));

  if (resolved->HasOwnProperty(v8::String::New("minimumSignificantDigits"))) {
    resolved->Set(v8::String::New("minimumSignificantDigits"), v8::Integer::New(
        number_format->getMinimumSignificantDigits()));
  }

  if (resolved->HasOwnProperty(v8::String::New("maximumSignificantDigits"))) {
    resolved->Set(v8::String::New("maximumSignificantDigits"), v8::Integer::New(
        number_format->getMaximumSignificantDigits()));
  }

  // Set the locale
  char result[ULOC_FULLNAME_CAPACITY];
  status = U_ZERO_ERROR;
  uloc_toLanguageTag(
      icu_locale.getName(), result, ULOC_FULLNAME_CAPACITY, FALSE, &status);
  if (U_SUCCESS(status)) {
    resolved->Set(v8::String::New("locale"), v8::String::New(result));
  } else {
    // This would never happen, since we got the locale from ICU.
    resolved->Set(v8::String::New("locale"), v8::String::New("und"));
  }
}

}  // namespace v8_i18n
