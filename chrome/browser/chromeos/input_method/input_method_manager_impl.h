// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_INPUT_METHOD_INPUT_METHOD_MANAGER_IMPL_H_
#define CHROME_BROWSER_CHROMEOS_INPUT_METHOD_INPUT_METHOD_MANAGER_IMPL_H_

#include <map>
#include <string>
#include <vector>

#include "base/observer_list.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/chromeos/input_method/browser_state_monitor.h"
#include "chrome/browser/chromeos/input_method/candidate_window_controller.h"
#include "chrome/browser/chromeos/input_method/ibus_controller.h"
#include "chrome/browser/chromeos/input_method/input_method_manager.h"
#include "chrome/browser/chromeos/input_method/input_method_util.h"
#include "chrome/browser/chromeos/input_method/input_method_whitelist.h"
#include "chrome/browser/chromeos/input_method/xkeyboard.h"

namespace chromeos {
class InputMethodEngineIBus;
namespace input_method {

// The implementation of InputMethodManager.
class InputMethodManagerImpl : public InputMethodManager,
                               public CandidateWindowController::Observer,
                               public IBusController::Observer {
 public:
  virtual ~InputMethodManagerImpl();

  // InputMethodManager override:
  virtual void AddObserver(InputMethodManager::Observer* observer) OVERRIDE;
  virtual void AddCandidateWindowObserver(
      InputMethodManager::CandidateWindowObserver* observer) OVERRIDE;
  virtual void RemoveObserver(InputMethodManager::Observer* observer) OVERRIDE;
  virtual void RemoveCandidateWindowObserver(
      InputMethodManager::CandidateWindowObserver* observer) OVERRIDE;
  virtual void SetState(State new_state) OVERRIDE;
  virtual InputMethodDescriptors* GetSupportedInputMethods() const OVERRIDE;
  virtual InputMethodDescriptors* GetActiveInputMethods() const OVERRIDE;
  virtual size_t GetNumActiveInputMethods() const OVERRIDE;
  virtual void EnableLayouts(const std::string& language_code,
                             const std::string& initial_layout) OVERRIDE;
  virtual bool EnableInputMethods(
      const std::vector<std::string>& new_active_input_method_ids) OVERRIDE;
  virtual bool SetInputMethodConfig(
      const std::string& section,
      const std::string& config_name,
      const InputMethodConfigValue& value) OVERRIDE;
  virtual void ChangeInputMethod(const std::string& input_method_id) OVERRIDE;
  virtual void ActivateInputMethodProperty(const std::string& key) OVERRIDE;
  virtual void AddInputMethodExtension(
      const std::string& id,
      const std::string& name,
      const std::vector<std::string>& layouts,
      const std::string& language,
      InputMethodEngine* instance) OVERRIDE;
  virtual void RemoveInputMethodExtension(const std::string& id) OVERRIDE;
  virtual void GetInputMethodExtensions(
      InputMethodDescriptors* result) OVERRIDE;
  virtual void SetFilteredExtensionImes(std::vector<std::string>* ids) OVERRIDE;
  virtual bool SwitchToNextInputMethod() OVERRIDE;
  virtual bool SwitchToPreviousInputMethod() OVERRIDE;
  virtual bool SwitchInputMethod(const ui::Accelerator& accelerator) OVERRIDE;
  virtual InputMethodDescriptor GetCurrentInputMethod() const OVERRIDE;
  virtual InputMethodPropertyList
      GetCurrentInputMethodProperties() const OVERRIDE;
  virtual XKeyboard* GetXKeyboard() OVERRIDE;
  virtual InputMethodUtil* GetInputMethodUtil() OVERRIDE;

  // Sets |ibus_controller_|.
  void SetIBusControllerForTesting(IBusController* ibus_controller);
  // Sets |candidate_window_controller_|.
  void SetCandidateWindowControllerForTesting(
      CandidateWindowController* candidate_window_controller);
  // Sets |xkeyboard_|.
  void SetXKeyboardForTesting(XKeyboard* xkeyboard);

  // Creates a new instance of this class. The caller has to delete the returned
  // object. The caller also have to set a mock CandidateWindowController,
  // IBusController, and XKeyboard. See the setters above.
  static InputMethodManagerImpl* GetInstanceForTesting();

 private:
  friend class InputMethodManager;
  InputMethodManagerImpl();

  // IBusController overrides:
  virtual void PropertyChanged() OVERRIDE;
  virtual void OnConnected() OVERRIDE;
  virtual void OnDisconnected() OVERRIDE;


  // CandidateWindowController::Observer overrides:
  virtual void CandidateWindowOpened() OVERRIDE;
  virtual void CandidateWindowClosed() OVERRIDE;

  // Attach IBusController, CandidateWindowController, and XKeyboard objects
  // to the InputMethodManagerImpl object. You don't have to call this function
  // if you attach them yourself (e.g. in unit tests) using the setters above.
  void Init();

  // Temporarily deactivates all input methods (e.g. Chinese, Japanese, Arabic)
  // since they are not necessary to input a login password. Users are still
  // able to use/switch active keyboard layouts (e.g. US qwerty, US dvorak,
  // French).
  void OnScreenLocked();

  // Resumes the original state by activating input methods and/or changing the
  // current input method as needed.
  void OnScreenUnlocked();

  // Returns true if |input_method_id| is in |active_input_method_ids_|.
  bool InputMethodIsActivated(const std::string& input_method_id);

  // Returns true if the given input method config value is a string list
  // that only contains an input method ID of a keyboard layout.
  bool ContainOnlyKeyboardLayout(const std::vector<std::string>& value);

  // Creates and initializes |candidate_window_controller_| if it hasn't been
  // done.
  void MaybeInitializeCandidateWindowController();

  // If |current_input_method_id_| is not in |input_method_ids|, switch to
  // input_method_ids[0]. If the ID is equal to input_method_ids[N], switch to
  // input_method_ids[N+1].
  void SwitchToNextInputMethodInternal(
      const std::vector<std::string>& input_method_ids,
      const std::string& current_input_method_id);

  void ChangeInputMethodInternal(const std::string& input_method_id,
                                 bool show_message);

  // The current browser status.
  State state_;

  // A list of objects that monitor the manager.
  ObserverList<InputMethodManager::Observer> observers_;
  ObserverList<CandidateWindowObserver> candidate_window_observers_;

  // The input method which was/is selected.
  InputMethodDescriptor previous_input_method_;
  InputMethodDescriptor current_input_method_;
  // The active input method ids cache.
  std::vector<std::string> active_input_method_ids_;

  // The list of IMEs that are filtered from the IME list.
  std::vector<std::string> filtered_extension_imes_;

  // For screen locker. When the screen is locked, |previous_input_method_|,
  // |current_input_method_|, and |active_input_method_ids_| above are copied
  // to these "saved" variables.
  InputMethodDescriptor saved_previous_input_method_;
  InputMethodDescriptor saved_current_input_method_;
  std::vector<std::string> saved_active_input_method_ids_;

  // Extra input methods that have been explicitly added to the menu, such as
  // those created by extension.
  std::map<std::string, InputMethodDescriptor> extra_input_methods_;
  std::map<std::string, InputMethodEngineIBus*> extra_input_method_instances_;

  // The browser state monitor is used to receive notifications from the browser
  // and call SetState() method of |this| class.
  scoped_ptr<BrowserStateMonitor> browser_state_monitor_;

  // The IBus controller is used to control the input method status and
  // allow callbacks when the input method status changes.
  scoped_ptr<IBusController> ibus_controller_;

  // The candidate window.  This will be deleted when the APP_TERMINATING
  // message is sent.
  scoped_ptr<CandidateWindowController> candidate_window_controller_;

  // The object which can create an InputMethodDescriptor object.
  InputMethodWhitelist whitelist_;

  // An object which provides miscellaneous input method utility functions. Note
  // that |util_| is required to initialize |xkeyboard_|.
  InputMethodUtil util_;

  // An object for switching XKB layouts and keyboard status like caps lock and
  // auto-repeat interval.
  scoped_ptr<XKeyboard> xkeyboard_;

  DISALLOW_COPY_AND_ASSIGN(InputMethodManagerImpl);
};

}  // namespace input_method
}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_INPUT_METHOD_INPUT_METHOD_MANAGER_IMPL_H_
