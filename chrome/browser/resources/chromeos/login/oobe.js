// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Out of the box experience flow (OOBE).
 * This is the main code for the OOBE WebUI implementation.
 */

var localStrings = new LocalStrings();

cr.define('cr.ui', function() {
  var DisplayManager = cr.ui.login.DisplayManager;

  /**
  * Constructs an Out of box controller. It manages initialization of screens,
  * transitions, error messages display.
  * @extends {DisplayManager}
  * @constructor
  */
  function Oobe() {
  }

  cr.addSingletonGetter(Oobe);

  Oobe.prototype = {
    __proto__: DisplayManager.prototype,
  };

  /**
   * Setups given "select" element using the list and adds callback.
   * @param {!Element} select Select object to be updated.
   * @param {!Object} list List of the options to be added.
   * @param {string} callback Callback name which should be send to Chrome or
   * an empty string if the event listener shouldn't be added.
   */
  Oobe.setupSelect = function(select, list, callback) {
    select.options.length = 0;
    for (var i = 0; i < list.length; ++i) {
      var item = list[i];
      var option =
          new Option(item.title, item.value, item.selected, item.selected);
      select.appendChild(option);
    }
    if (callback) {
      var send_callback = function() {
        chrome.send(callback, [select.options[select.selectedIndex].value]);
      };
      select.addEventListener('blur', function(event) { send_callback(); });
      select.addEventListener('click', function(event) { send_callback(); });
      select.addEventListener('keyup', function(event) {
        var keycode_interested = [
          9,  // Tab
          13,  // Enter
          27,  // Escape
        ];
        if (keycode_interested.indexOf(event.keyCode) >= 0)
          send_callback();
      });
    }
  }

  /**
   * Initializes the OOBE flow.  This will cause all C++ handlers to
   * be invoked to do final setup.
   */
  Oobe.initialize = function() {
    oobe.NetworkScreen.register();
    oobe.EulaScreen.register();
    oobe.UpdateScreen.register();
    oobe.OAuthEnrollmentScreen.register();
    oobe.ResetScreen.register();
    login.AccountPickerScreen.register();
    login.GaiaSigninScreen.register();
    oobe.UserImageScreen.register(/* lazyInit= */ false);
    login.ErrorMessageScreen.register();
    login.TPMErrorMessageScreen.register();

    cr.ui.Bubble.decorate($('bubble'));
    login.HeaderBar.decorate($('login-header-bar'));

    // TODO: Cleanup with old OOBE style removal.
    $('security-link').addEventListener('click', function(event) {
      chrome.send('eulaOnTpmPopupOpened');
      $('popup-overlay').hidden = false;
      $('security-ok-button').focus();
    });
    $('security-tpm-link').addEventListener('click', function(event) {
      chrome.send('eulaOnTpmPopupOpened');
      $('popup-overlay').hidden = false;
      $('security-ok-button').focus();
    });
    $('security-ok-button').addEventListener('click', function(event) {
      $('popup-overlay').hidden = true;
    });
    // Do not allow focus leaving the overlay.
    $('popup-overlay').addEventListener('focusout', function(event) {
      // WebKit does not allow immediate focus return.
      setTimeout(function() { $('security-ok-button').focus(); }, 0);
      event.preventDefault();
    });

    chrome.send('screenStateInitialize');
  };

  /**
   * Handle accelerators. These are passed from native code instead of a JS
   * event handler in order to make sure that embedded iframes cannot swallow
   * them.
   * @param {string} name Accelerator name.
   */
  Oobe.handleAccelerator = function(name) {
    Oobe.getInstance().handleAccelerator(name);
  };

  /**
   * Shows the given screen.
   * @param {Object} screen Screen params dict, e.g. {id: screenId, data: data}
   */
  Oobe.showScreen = function(screen) {
    Oobe.getInstance().showScreen(screen);
  };

  /**
   * Enables/disables continue button.
   * @param {boolean} enable Should the button be enabled?
   */
  Oobe.enableContinueButton = function(enable) {
    $('continue-button').disabled = !enable;
  };

  /**
   * Sets usage statistics checkbox.
   * @param {boolean} checked Is the checkbox checked?
   */
  Oobe.setUsageStats = function(checked) {
    $('usage-stats').checked = checked;
  };

  /**
   * Set OEM EULA URL.
   * @param {text} oemEulaUrl OEM EULA URL.
   */
  Oobe.setOemEulaUrl = function(oemEulaUrl) {
    if (oemEulaUrl) {
      $('oem-eula-frame').src = oemEulaUrl;
      $('eulas').classList.remove('one-column');
    } else {
      $('eulas').classList.add('one-column');
    }
  };

  /**
   * Sets update's progress bar value.
   * @param {number} progress Percentage of the progress bar.
   */
  Oobe.setUpdateProgress = function(progress) {
    $('update-progress-bar').value = progress;
  };

  /**
   * Shows or hides downloading ETA message.
   * @param {boolean} visible Are ETA message visible?
   */
  Oobe.showEstimatedTimeLeft = function(visible) {
    $('progress-message').hidden = visible;
    $('estimated-time-left').hidden = !visible;
  };

  /**
   * Sets estimated time left until download will complete.
   * @param {number} seconds Time left in seconds.
   */
  Oobe.setEstimatedTimeLeft = function(seconds) {
    var minutes = Math.ceil(seconds / 60);
    var message = '';
    if (minutes > 60) {
      message = localStrings.getString('downloadingTimeLeftLong');
    } else if (minutes > 55) {
      message = localStrings.getString('downloadingTimeLeftStatusOneHour');
    } else if (minutes > 20) {
      message = localStrings.getStringF('downloadingTimeLeftStatusMinutes',
                                        Math.ceil(minutes / 5) * 5);
    } else if (minutes > 1) {
      message = localStrings.getStringF('downloadingTimeLeftStatusMinutes',
                                        minutes);
    } else {
      message = localStrings.getString('downloadingTimeLeftSmall');
    }
    $('estimated-time-left').textContent =
      localStrings.getStringF('downloading', message);
  };

  /**
   * Shows or hides info message below progress bar.
   * @param {boolean} visible Are message visible?
   */
  Oobe.showProgressMessage = function(visible) {
    $('estimated-time-left').hidden = visible;
    $('progress-message').hidden = !visible;
  };

  /**
   * Sets message below progress bar.
   * @param {string} message Message that should be shown.
   */
  Oobe.setProgressMessage = function(message) {
    $('progress-message').innerText = message;
  };

  /**
   * Sets update message, which is shown above the progress bar.
   * @param {text} message Message which is shown by the label.
   */
  Oobe.setUpdateMessage = function(message) {
    $('update-upper-label').textContent = message;
  };

  /**
   * Shows or hides update curtain.
   * @param {boolean} visible Are curtains visible?
   */
  Oobe.showUpdateCurtain = function(visible) {
    $('update-screen-curtain').hidden = !visible;
    $('update-screen-main').hidden = visible;
  };

  /**
   * Sets TPM password.
   * @param {text} password TPM password to be shown.
   */
  Oobe.setTpmPassword = function(password) {
    $('tpm-busy').hidden = true;
    $('tpm-password').textContent = password;
    $('tpm-password').hidden = false;
  };

  /**
   * Reloads content of the page (localized strings, options of the select
   * controls).
   * @param {!Object} data New dictionary with i18n values.
   */
  Oobe.reloadContent = function(data) {
    // Reload global local strings, process DOM tree again.
    templateData = data;
    i18nTemplate.process(document, data);

    // Update language and input method menu lists.
    Oobe.setupSelect($('language-select'), data.languageList, '');
    Oobe.setupSelect($('keyboard-select'), data.inputMethodsList, '');

    // Update localized content of the screens.
    Oobe.updateLocalizedContent();
  };

  /**
   * Updates version label visibilty.
   * @param {boolean} show True if version label should be visible.
   */
  Oobe.showVersion = function(show) {
    Oobe.getInstance().showVersion(show);
  };

  /**
   * Updates localized content of the screens.
   * Should be executed on language change.
   */
  Oobe.updateLocalizedContent = function() {
    // Buttons, headers and links.
    Oobe.getInstance().updateLocalizedContent_();
  };

  /**
   * Update body class to switch between OOBE UI and Login UI.
   */
  Oobe.showOobeUI = function(showOobe) {
    if (showOobe) {
      document.body.classList.remove('login-display');
    } else {
      document.body.classList.add('login-display');
      Oobe.getInstance().prepareForLoginDisplay_();
    }

    // Don't show header bar for OOBE.
    Oobe.getInstance().headerHidden = showOobe;
  };

  /**
   * Disables signin UI.
   */
  Oobe.disableSigninUI = function() {
    DisplayManager.disableSigninUI();
  };

  /**
   * Shows signin UI.
   * @param {string} opt_email An optional email for signin UI.
   */
  Oobe.showSigninUI = function(opt_email) {
    DisplayManager.showSigninUI(opt_email);
  };

  /**
   * Resets sign-in input fields.
   * @param {boolean} forceOnline Whether online sign-in should be forced.
   * If |forceOnline| is false previously used sign-in type will be used.
   */
  Oobe.resetSigninUI = function(forceOnline) {
    DisplayManager.resetSigninUI(forceOnline);
  };

  /**
   * Shows sign-in error bubble.
   * @param {number} loginAttempts Number of login attemps tried.
   * @param {string} message Error message to show.
   * @param {string} link Text to use for help link.
   * @param {number} helpId Help topic Id associated with help link.
   */
  Oobe.showSignInError = function(loginAttempts, message, link, helpId) {
    DisplayManager.showSignInError(loginAttempts, message, link, helpId);
  };

  /**
   * Shows TPM error screen.
   */
  Oobe.showTpmError = function() {
    DisplayManager.showTpmError();
  };

  /**
   * Clears error bubble.
   */
  Oobe.clearErrors = function() {
    DisplayManager.clearErrors();
  };

  /**
   * Displays animations on successful authentication, that have to happen
   * before login UI is dismissed.
   */
  Oobe.animateAuthenticationSuccess = function() {
    login.HeaderBar.animateOut(function() {
      chrome.send('unlockOnLoginSuccess');
    });
  };

  /**
   * Displays animations that have to happen once login UI is fully displayed.
   */
  Oobe.animateOnceFullyDisplayed = function() {
    login.HeaderBar.animateIn();
  };

  /**
   * Handles login success notification.
   */
  Oobe.onLoginSuccess = function(username) {
    if (Oobe.getInstance().currentScreen.id == SCREEN_ACCOUNT_PICKER) {
      // TODO(nkostylev): Enable animation back when session start jank
      // is reduced. See http://crosbug.com/11116 http://crosbug.com/18307
      // $('pod-row').startAuthenticatedAnimation();
    }
  };

  /**
   * Sets text content for a div with |labelId|.
   * @param {string} labelId Id of the label div.
   * @param {string} labelText Text for the label.
   */
  Oobe.setLabelText = function(labelId, labelText) {
    DisplayManager.setLabelText(labelId, labelText);
  };

  // Export
  return {
    Oobe: Oobe
  };
});

var Oobe = cr.ui.Oobe;

// Allow selection events on components with editable text (password field)
// bug (http://code.google.com/p/chromium/issues/detail?id=125863)
disableTextSelectAndDrag(function(e) {
  var src = e.target;
  return src instanceof HTMLTextAreaElement ||
         src instanceof HTMLInputElement &&
         /text|password|search/.test(src.type);
});

document.addEventListener('DOMContentLoaded', cr.ui.Oobe.initialize);
