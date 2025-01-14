// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview User pod row implementation.
 */

cr.define('login', function() {
  /**
   * Pod width. 170px Pod + 10px padding + 10px margin on both sides.
   * @type {number}
   * @const
   */
  var POD_WIDTH = 170 + 2 * (10 + 10);

  /**
   * Whether to preselect the first pod automatically on login screen.
   * @type {boolean}
   * @const
   */
  var PRESELECT_FIRST_POD = true;

  /**
   * Wallpaper load delay in milliseconds.
   * @type {number}
   * @const
   */
  var WALLPAPER_LOAD_DELAY_MS = 500;

  /**
   * Wallpaper load delay in milliseconds. TODO(nkostylev): Tune this constant.
   * @type {number}
   * @const
   */
  var WALLPAPER_BOOT_LOAD_DELAY_MS = 100;

  /**
   * Maximum time for which the pod row remains hidden until all user images
   * have been loaded.
   * @type {number}
   * @const
   */
  var POD_ROW_IMAGES_LOAD_TIMEOUT_MS = 3000;

  /**
   * Oauth token status. These must match UserManager::OAuthTokenStatus.
   * @enum {number}
   * @const
   */
  var OAuthTokenStatus = {
    UNKNOWN: 0,
    INVALID: 1,
    VALID: 2
  };

  /**
   * Tab order for user pods. Update these when adding new controls.
   * @enum {number}
   * @const
   */
  var UserPodTabOrder = {
    POD_INPUT: 1,    // Password input fields (and whole pods themselves).
    HEADER_BAR: 2,   // Buttons on the header bar (Shutdown, Add User).
    REMOVE_USER: 3   // Remove ('X') buttons.
  };

  // Focus and tab order are organized as follows:
  //
  // (1) all user pods have tab index 1 so they are traversed first;
  // (2) when a user pod is activated, its tab index is set to -1 and its
  //     main input field gets focus and tab index 1;
  // (3) buttons on the header bar have tab index 2 so they follow user pods;
  // (4) 'Remove' buttons have tab index 3 and follow header bar buttons;
  // (5) lastly, focus jumps to the Status Area and back to user pods.
  //
  // 'Focus' event is handled by a capture handler for the whole document
  // and in some cases 'mousedown' event handlers are used instead of 'click'
  // handlers where it's necessary to prevent 'focus' event from being fired.

  /**
   * Helper function to remove a class from given element.
   * @param {!HTMLElement} el Element whose class list to change.
   * @param {string} cl Class to remove.
   */
  function removeClass(el, cl) {
    el.classList.remove(cl);
  }

  /**
   * Creates a user pod.
   * @constructor
   * @extends {HTMLDivElement}
   */
  var UserPod = cr.ui.define(function() {
    return $('user-pod-template').cloneNode(true);
  });

  UserPod.prototype = {
    __proto__: HTMLDivElement.prototype,

    /** @inheritDoc */
    decorate: function() {
      this.tabIndex = UserPodTabOrder.POD_INPUT;
      this.removeUserButtonElement.tabIndex = UserPodTabOrder.REMOVE_USER;

      // Mousedown has to be used instead of click to be able to prevent 'focus'
      // event later.
      this.addEventListener('mousedown',
          this.handleMouseDown_.bind(this));

      this.signinButtonElement.addEventListener('click',
          this.activate.bind(this));

      this.removeUserButtonElement.addEventListener('mousedown', function(e) {
        // Prevent default so that we don't trigger a 'focus' event.
        e.preventDefault();
        // Prevent the 'mousedown' event for the whole pod, which could result
        // in sign-in UI being shown.
        e.stopPropagation();
      });
      this.removeUserButtonElement.addEventListener('click',
          this.handleRemoveButtonClick_.bind(this));
      this.removeUserButtonElement.addEventListener('mouseout',
          this.handleRemoveButtonMouseOutOrBlur_.bind(this));
      // TODO(altimofeev): this will trigger when Gaia extension grabs focus.
      this.removeUserButtonElement.addEventListener('blur',
          this.handleRemoveButtonMouseOutOrBlur_.bind(this));
    },

    /**
     * Initializes the pod after its properties set and added to a pod row.
     */
    initialize: function() {
      this.passwordElement.addEventListener('keydown',
          this.parentNode.handleKeyDown.bind(this.parentNode));
      this.passwordElement.addEventListener('keypress',
          this.handlePasswordKeyPress_.bind(this));

      this.imageElement.addEventListener('load',
          this.parentNode.handlePodImageLoad.bind(this.parentNode, this));
    },

    /**
     * Resets tab order for pod elements to its initial state.
     */
    resetTabOrder: function() {
      this.tabIndex = UserPodTabOrder.POD_INPUT;
      this.mainInput.tabIndex = -1;
    },

    /**
     * Handles keypress event (i.e. any textual input) on password input.
     * @param {Event} e Keypress Event object.
     * @private
     */
    handlePasswordKeyPress_: function(e) {
      // When tabbing from the system tray a tab key press is received. Suppress
      // this so as not to type a tab character into the password field.
      if (e.keyCode == 9) {
        e.preventDefault();
        return;
      }
    },

    /**
     * Gets signed in indicator element.
     * @type {!HTMLDivElement}
     */
    get signedInIndicatorElement() {
      return this.firstElementChild;
    },

    /**
     * Gets image element.
     * @type {!HTMLImageElement}
     */
    get imageElement() {
      return this.signedInIndicatorElement.nextElementSibling;
    },

    /**
     * Gets name element.
     * @type {!HTMLDivElement}
     */
    get nameElement() {
      return this.imageElement.nextElementSibling;
    },

    /**
     * Gets password field.
     * @type {!HTMLInputElement}
     */
    get passwordElement() {
      return this.nameElement.nextElementSibling;
    },

    /**
     * Gets Caps Lock hint image.
     * @type {!HTMLImageElement}
     */
    get capslockHintElement() {
      return this.signinButtonElement.previousElementSibling;
    },

    /**
     * Gets user signin button.
     * @type {!HTMLInputElement}
     */
    get signinButtonElement() {
      return this.removeUserButtonElement.previousElementSibling;
    },

    /**
     * Gets remove user button.
     * @type {!HTMLInputElement}
     */
    get removeUserButtonElement() {
      return this.lastElementChild;
    },

    /**
     * Updates the user pod element.
     */
    update: function() {
      this.updateUserImage();

      this.nameElement.textContent = this.user_.displayName;
      this.removeUserButtonElement.hidden = !this.user_.canRemove;
      this.signedInIndicatorElement.hidden = !this.user_.signedIn;

      var needSignin = this.needGaiaSignin;
      this.passwordElement.hidden = needSignin;
      this.removeUserButtonElement.setAttribute(
          'aria-label', localStrings.getStringF('removeButtonAccessibleName',
                                                this.user_.emailAddress));
      this.passwordElement.setAttribute('aria-label',
                                        localStrings.getStringF(
                                            'passwordFieldAccessibleName',
                                            this.user_.emailAddress));
      this.signinButtonElement.hidden = !needSignin;
    },

    /**
     * The user that this pod represents.
     * @type {!Object}
     */
    user_: undefined,
    get user() {
      return this.user_;
    },
    set user(userDict) {
      this.user_ = userDict;
      this.update();
    },

    /**
     * Whether Gaia signin is required for this user.
     */
    get needGaiaSignin() {
      // Gaia signin is performed if the user has an invalid oauth token and is
      // not currently signed in (i.e. not the lock screen).
      return this.user.oauthTokenStatus != OAuthTokenStatus.VALID &&
          !this.user.signedIn;
    },

    /**
     * Gets main input element.
     * @type {(HTMLButtonElement|HTMLInputElement)}
     */
    get mainInput() {
      if (!this.signinButtonElement.hidden)
        return this.signinButtonElement;
      else
        return this.passwordElement;
    },

    /**
     * Whether remove button is active state.
     * @type {boolean}
     */
    get activeRemoveButton() {
      return this.removeUserButtonElement.classList.contains('active');
    },
    set activeRemoveButton(active) {
      if (active == this.activeRemoveButton)
        return;

      if (active) {
        // Clear focus first if another pod is focused.
        if (!this.parentNode.isFocused(this))
          this.parentNode.focusPod(undefined, true);
        this.removeUserButtonElement.textContent =
            localStrings.getString('removeUser');
        this.removeUserButtonElement.classList.add('active');
      } else {
        this.removeUserButtonElement.textContent = '';
        this.removeUserButtonElement.classList.remove('active');
      }
    },

    /**
     * Updates the image element of the user.
     */
    updateUserImage: function() {
      this.imageElement.src = 'chrome://userimage/' + this.user.username +
          '?id=' + new Date().getTime();
    },

    /**
     * Focuses on input element.
     */
    focusInput: function() {
      var needSignin = this.needGaiaSignin;
      this.signinButtonElement.hidden = !needSignin;
      this.passwordElement.hidden = needSignin;

      // Move tabIndex from the whole pod to the main input.
      this.tabIndex = -1;
      this.mainInput.tabIndex = UserPodTabOrder.POD_INPUT;
      this.mainInput.focus();
    },

    /**
     * Activates the pod.
     * @return {boolean} True if activated successfully.
     */
    activate: function() {
      if (!this.signinButtonElement.hidden) {
        // Switch to Gaia signin.
        this.showSigninUI();
      } else if (!this.passwordElement.value) {
        return false;
      } else {
        Oobe.disableSigninUI();
        chrome.send('authenticateUser',
                    [this.user.username, this.passwordElement.value]);
      }

      return true;
    },

    /**
     * Shows signin UI for this user.
     */
    showSigninUI: function() {
      this.parentNode.showSigninUI(this.user.emailAddress);
    },

    /**
     * Resets the input field and updates the tab order of pod controls.
     * @param {boolean} takeFocus If true, input field takes focus.
     */
    reset: function(takeFocus) {
      this.passwordElement.value = '';
      if (takeFocus)
        this.focusInput();  // This will set a custom tab order.
      else
        this.resetTabOrder();
    },

    /**
     * Handles mouseout and blur on remove button.
     * @param {Event} e Mouseout or blur event.
     */
    handleRemoveButtonMouseOutOrBlur_: function(e) {
      this.activeRemoveButton = false;
    },

    /**
     * Handles a click event on remove user button.
     * @param {Event} e Click event.
     */
    handleRemoveButtonClick_: function(e) {
      if (this.parentNode.disabled)
        return;
      if (this.activeRemoveButton)
        chrome.send('removeUser', [this.user.username]);
      else
        this.activeRemoveButton = true;
    },

    /**
     * Handles mousedown event on a user pod.
     * @param {Event} e Mouseout event.
     */
    handleMouseDown_: function(e) {
      if (this.parentNode.disabled)
        return;
      if (!this.signinButtonElement.hidden) {
        this.showSigninUI();
        // Prevent default so that we don't trigger 'focus' event.
        e.preventDefault();
      }
    }
  };


  /**
   * Creates a new pod row element.
   * @constructor
   * @extends {HTMLDivElement}
   */
  var PodRow = cr.ui.define('podrow');

  PodRow.prototype = {
    __proto__: HTMLDivElement.prototype,

    // Whether this user pod row is shown for the first time.
    firstShown_: true,

    // Whether the initial wallpaper load after boot has been requested. Used
    // only if |Oobe.getInstance().shouldLoadWallpaperOnBoot()| is true.
    bootWallpaperLoaded_: false,

    // True if inside focusPod().
    insideFocusPod_: false,

    // True if user pod has been activated with keyboard.
    // In case of activation with keyboard we delay wallpaper change.
    keyboardActivated_: false,

    // Focused pod.
    focusedPod_: undefined,

    // Activated pod, i.e. the pod of current login attempt.
    activatedPod_: undefined,

    // Pod that was most recently focused, if any.
    lastFocusedPod_: undefined,

    // When moving through users quickly at login screen, set a timeout to
    // prevent loading intermediate wallpapers.
    loadWallpaperTimeout_: null,

    // Pods whose initial images haven't been loaded yet.
    podsWithPendingImages_: [],

    /** @inheritDoc */
    decorate: function() {
      this.style.left = 0;

      // Event listeners that are installed for the time period during which
      // the element is visible.
      this.listeners_ = {
        focus: [this.handleFocus_.bind(this), true],
        click: [this.handleClick_.bind(this), false],
        keydown: [this.handleKeyDown.bind(this), false]
      };
    },

    /**
     * Returns all the pods in this pod row.
     * @type {NodeList}
     */
    get pods() {
      return this.children;
    },

    /**
     * Return true if user pod row has only single user pod in it.
     * @type {boolean}
     */
    get isSinglePod() {
      return this.children.length == 1;
    },

    hideTitles: function() {
      for (var i = 0, pod; pod = this.pods[i]; ++i)
        pod.imageElement.title = '';
    },

    updateTitles: function() {
      for (var i = 0, pod; pod = this.pods[i]; ++i) {
        pod.imageElement.title = pod.user.nameTooltip || '';
      }
    },

    /**
     * Returns pod with the given username (null if there is no such pod).
     * @param {string} username Username to be matched.
     * @return {Object} Pod with the given username. null if pod hasn't been
     *                  found.
     */
    getPodWithUsername_: function(username) {
      for (var i = 0, pod; pod = this.pods[i]; ++i) {
        if (pod.user.username == username)
          return pod;
      }
      return null;
    },

    /**
     * True if the the pod row is disabled (handles no user interaction).
     * @type {boolean}
     */
    disabled_: false,
    get disabled() {
      return this.disabled_;
    },
    set disabled(value) {
      this.disabled_ = value;
      var controls = this.querySelectorAll('button,input');
      for (var i = 0, control; control = controls[i]; ++i) {
        control.disabled = value;
      }
    },

    /**
     * Creates a user pod from given email.
     * @param {string} email User's email.
     */
    createUserPod: function(user) {
      var userPod = new UserPod({user: user});
      userPod.hidden = false;
      return userPod;
    },

    /**
     * Add an existing user pod to this pod row.
     * @param {!Object} user User info dictionary.
     * @param {boolean} animated Whether to use init animation.
     */
    addUserPod: function(user, animated) {
      var userPod = this.createUserPod(user);
      if (animated) {
        userPod.classList.add('init');
        userPod.nameElement.classList.add('init');
      }

      this.appendChild(userPod);
      userPod.initialize();
    },

    /**
     * Returns index of given pod or -1 if not found.
     * @param {UserPod} pod Pod to look up.
     * @private
     */
    indexOf_: function(pod) {
      for (var i = 0; i < this.pods.length; ++i) {
        if (pod == this.pods[i])
          return i;
      }
      return -1;
    },

    /**
     * Ensures the given pod is visible.
     * @param {UserPod} pod Pod to scroll into view.
     */
    scrollPodIntoView: function(pod) {
      var podIndex = this.indexOf_(pod);
      if (podIndex == -1)
        return;

      var left = podIndex * POD_WIDTH;
      var right = left + POD_WIDTH;

      var viewportLeft = -parseInt(this.style.left);
      var viewportRight = viewportLeft + this.parentNode.clientWidth;

      if (left < viewportLeft) {
        this.style.left = -left + 'px';
      } else if (right > viewportRight) {
        var offset = right - viewportRight;
        this.style.left = (viewportLeft - offset) + 'px';
      }
    },

    /**
     * Start first time show animation.
     */
    startInitAnimation: function() {
      // Schedule init animation.
      for (var i = 0, pod; pod = this.pods[i]; ++i) {
        window.setTimeout(removeClass, 500 + i * 70, pod, 'init');
        window.setTimeout(removeClass, 700 + i * 70, pod.nameElement, 'init');
      }
    },

    /**
     * Start login success animation.
     */
    startAuthenticatedAnimation: function() {
      var activated = this.indexOf_(this.activatedPod_);
      if (activated == -1)
        return;

      for (var i = 0, pod; pod = this.pods[i]; ++i) {
        if (i < activated)
          pod.classList.add('left');
        else if (i > activated)
          pod.classList.add('right');
        else
          pod.classList.add('zoom');
      }
    },

    /**
     * Populates pod row with given existing users and start init animation.
     * @param {array} users Array of existing user emails.
     * @param {boolean} animated Whether to use init animation.
     */
    loadPods: function(users, animated) {
      // Clear existing pods.
      this.innerHTML = '';
      this.focusedPod_ = undefined;
      this.activatedPod_ = undefined;
      this.lastFocusedPod_ = undefined;

      // Populate the pod row.
      for (var i = 0; i < users.length; ++i) {
        this.addUserPod(users[i], animated);
      }
      for (var i = 0, pod; pod = this.pods[i]; ++i) {
        this.podsWithPendingImages_.push(pod);
      }
      // Make sure we eventually show the pod row, even if some image is stuck.
      setTimeout(function() {
        $('pod-row').classList.remove('images-loading');
      }, POD_ROW_IMAGES_LOAD_TIMEOUT_MS);

      // loadPods is called after user list update (for ex. after deleting user)
      // so make sure that tooltips are updated.
      $('pod-row').updateTitles();

      this.focusPod(this.preselectedPod);
    },

    /**
     * Whether the pod is currently focused.
     * @param {UserPod} pod Pod to check for focus.
     * @return {boolean} Pod focus status.
     */
    isFocused: function(pod) {
      return this.focusedPod_ == pod;
    },

    /**
     * Focuses a given user pod or clear focus when given null.
     * @param {UserPod=} podToFocus User pod to focus (undefined clears focus).
     * @param {boolean=} opt_force If true, forces focus update even when
     *                             podToFocus is already focused.
     */
    focusPod: function(podToFocus, opt_force) {
      if (this.isFocused(podToFocus) && !opt_force) {
        this.keyboardActivated_ = false;
        return;
      }

      // Make sure there's only one focusPod operation happening at a time.
      if (this.insideFocusPod_) {
        this.keyboardActivated_ = false;
        return;
      }
      this.insideFocusPod_ = true;

      clearTimeout(this.loadWallpaperTimeout_);
      for (var i = 0, pod; pod = this.pods[i]; ++i) {
        pod.activeRemoveButton = false;
        if (pod != podToFocus) {
          pod.classList.remove('focused');
          pod.classList.remove('faded');
          pod.reset(false);
        }
      }

      var hadFocus = !!this.focusedPod_;
      this.focusedPod_ = podToFocus;
      if (podToFocus) {
        podToFocus.classList.remove('faded');
        podToFocus.classList.add('focused');
        podToFocus.reset(true);  // Reset and give focus.
        this.scrollPodIntoView(podToFocus);
        if (hadFocus && this.keyboardActivated_) {
          // Delay wallpaper loading to let user tab through pods without lag.
          this.loadWallpaperTimeout_ = window.setTimeout(
              this.loadWallpaper_.bind(this), WALLPAPER_LOAD_DELAY_MS);
        } else if (!this.firstShown_) {
          // Load wallpaper immediately if there no pod was focused
          // previously, and it is not a boot into user pod list case.
          this.loadWallpaper_();
        }
        this.firstShown_ = false;
        this.lastFocusedPod_ = podToFocus;
      }
      this.insideFocusPod_ = false;
      this.keyboardActivated_ = false;
    },

    /**
     * Loads wallpaper for the active user pod, if any.
     * @private
     */
    loadWallpaper_: function() {
      if (this.focusedPod_)
        chrome.send('loadWallpaper', [this.focusedPod_.user.username]);
    },

    /**
     * Resets wallpaper to the last active user's wallpaper, if any.
     */
    loadLastWallpaper: function() {
      if (this.lastFocusedPod_)
        chrome.send('loadWallpaper', [this.lastFocusedPod_.user.username]);
    },

    /**
     * Returns the currently activated pod.
     * @type {UserPod}
     */
    get activatedPod() {
      return this.activatedPod_;
    },
    set activatedPod(pod) {
      if (pod && pod.activate())
        this.activatedPod_ = pod;
    },

    /**
     * The pod of the signed-in user, if any; null otherwise.
     * @type {?UserPod}
     */
    get lockedPod() {
      for (var i = 0, pod; pod = this.pods[i]; ++i) {
        if (pod.user.signedIn)
          return pod;
      }
      return null;
    },

    /**
     * The pod that is preselected on user pod row show.
     * @type {?UserPod}
     */
    get preselectedPod() {
      var lockedPod = this.lockedPod;
      var preselectedPod = PRESELECT_FIRST_POD ?
          lockedPod || this.pods[0] : lockedPod;
      return preselectedPod;
    },

    /**
     * Resets input UI.
     * @param {boolean} takeFocus True to take focus.
     */
    reset: function(takeFocus) {
      this.disabled = false;
      if (this.activatedPod_)
        this.activatedPod_.reset(takeFocus);
    },

    /**
     * Shows signin UI.
     * @param {string} email Email for signin UI.
     */
    showSigninUI: function(email) {
      // Clear any error messages that might still be around.
      Oobe.clearErrors();
      this.disabled = true;
      this.lastFocusedPod_ = this.getPodWithUsername_(email);
      Oobe.showSigninUI(email);
    },

    /**
     * Updates current image of a user.
     * @param {string} username User for which to update the image.
     */
    updateUserImage: function(username) {
      var pod = this.getPodWithUsername_(username);
      if (pod)
        pod.updateUserImage();
    },

    /**
     * Resets OAuth token status (invalidates it).
     * @param {string} username User for which to reset the status.
     */
    resetUserOAuthTokenStatus: function(username) {
      var pod = this.getPodWithUsername_(username);
      if (pod) {
        pod.user.oauthTokenStatus = OAuthTokenStatus.INVALID;
        pod.update();
      } else {
        console.log('Failed to update Gaia state for: ' + username);
      }
    },

    /**
     * Handler of click event.
     * @param {Event} e Click Event object.
     * @private
     */
    handleClick_: function(e) {
      if (this.disabled)
        return;
      // Clears focus if not clicked on a pod and if there's more than one pod.
      if (e.target.parentNode != this &&
          e.target.parentNode.parentNode != this &&
          !this.isSinglePod) {
        this.focusPod();
      }

      // Return focus back to single pod.
      if (this.isSinglePod) {
        this.focusPod(this.focusedPod_, true /* force */);
      }
    },

    /**
     * Handles focus event.
     * @param {Event} e Focus Event object.
     * @private
     */
    handleFocus_: function(e) {
      if (this.disabled)
        return;
      if (e.target.parentNode == this) {
        // Focus on a pod
        if (e.target.classList.contains('focused'))
          e.target.focusInput();
        else
          this.focusPod(e.target);
      } else if (e.target.parentNode.parentNode == this) {
        // Focus on a control of a pod but not on the Remove button.
        if (!e.target.parentNode.classList.contains('focused') &&
            !e.target.classList.contains('remove-user-button')) {
          this.focusPod(e.target.parentNode);
          e.target.focus();
        }
      } else {
        // Clears pod focus when we reach here. It means new focus is neither
        // on a pod nor on a button/input for a pod.
        // Do not "defocus" user pod when it is a single pod.
        // That means that 'focused' class will not be removed and
        // input field/button will always be visible.
        if (!this.isSinglePod)
          this.focusPod();
      }
    },

    /**
     * Handler of keydown event.
     * @param {Event} e KeyDown Event object.
     */
    handleKeyDown: function(e) {
      if (this.disabled)
        return;
      var editing = e.target.tagName == 'INPUT' && e.target.value;
      switch (e.keyIdentifier) {
        case 'Left':
          if (!editing) {
            this.keyboardActivated_ = true;
            if (this.focusedPod_ && this.focusedPod_.previousElementSibling)
              this.focusPod(this.focusedPod_.previousElementSibling);
            else
              this.focusPod(this.lastElementChild);

            e.stopPropagation();
          }
          break;
        case 'Right':
          if (!editing) {
            this.keyboardActivated_ = true;
            if (this.focusedPod_ && this.focusedPod_.nextElementSibling)
              this.focusPod(this.focusedPod_.nextElementSibling);
            else
              this.focusPod(this.firstElementChild);

            e.stopPropagation();
          }
          break;
        case 'Enter':
          if (this.focusedPod_) {
            this.activatedPod = this.focusedPod_;
            e.stopPropagation();
          }
          break;
        case 'U+001B':  // Esc
          if (!this.isSinglePod)
            this.focusPod();
          break;
      }
    },

    /**
     * Called right after the pod row is shown.
     */
    handleAfterShow: function() {
      // Force input focus for user pod on show and once transition ends.
      if (this.focusedPod_) {
        var focusedPod = this.focusedPod_;
        var screen = this.parentNode;
        var self = this;
        focusedPod.addEventListener('webkitTransitionEnd', function f(e) {
          if (e.target == focusedPod) {
            focusedPod.removeEventListener('webkitTransitionEnd', f);
            focusedPod.reset(true);
            // Notify screen that it is ready.
            screen.onShow();
            // Boot transition: load wallpaper.
            if (!self.bootWallpaperLoaded_ &&
                Oobe.getInstance().shouldLoadWallpaperOnBoot()) {
              self.loadWallpaperTimeout_ = window.setTimeout(
                  self.loadWallpaper_.bind(self), WALLPAPER_BOOT_LOAD_DELAY_MS);
              self.bootWallpaperLoaded_ = true;
            }
          }
        });
      }
    },

    /**
     * Called right before the pod row is shown.
     */
    handleBeforeShow: function() {
      for (var event in this.listeners_) {
        this.ownerDocument.addEventListener(
            event, this.listeners_[event][0], this.listeners_[event][1]);
      }
      $('login-header-bar').buttonsTabIndex = UserPodTabOrder.HEADER_BAR;
      this.updateTitles();
    },

    /**
     * Called when the element is hidden.
     */
    handleHide: function() {
      for (var event in this.listeners_) {
        this.ownerDocument.removeEventListener(
            event, this.listeners_[event][0], this.listeners_[event][1]);
      }
      $('login-header-bar').buttonsTabIndex = 0;
      this.hideTitles();
    },

    /**
     * Called when a pod's user image finishes loading.
     */
    handlePodImageLoad: function(pod) {
      var index = this.podsWithPendingImages_.indexOf(pod);
      if (index == -1) {
        return;
      }

      this.podsWithPendingImages_.splice(index, 1);
      if (this.podsWithPendingImages_.length == 0) {
        this.classList.remove('images-loading');
        chrome.send('userImagesLoaded');
      }
    }
  };

  return {
    PodRow: PodRow
  };
});
