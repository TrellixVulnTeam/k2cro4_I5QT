// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// require: array_data_model.js
// require: list_selection_model.js
// require: list_selection_controller.js
// require: list_item.js

/**
 * @fileoverview This implements a list control.
 */

cr.define('cr.ui', function() {
  /** @const */ var ListSelectionModel = cr.ui.ListSelectionModel;
  /** @const */ var ListSelectionController = cr.ui.ListSelectionController;
  /** @const */ var ArrayDataModel = cr.ui.ArrayDataModel;

  /**
   * Whether a mouse event is inside the element viewport. This will return
   * false if the mouseevent was generated over a border or a scrollbar.
   * @param {!HTMLElement} el The element to test the event with.
   * @param {!Event} e The mouse event.
   * @param {boolean} Whether the mouse event was inside the viewport.
   */
  function inViewport(el, e) {
    var rect = el.getBoundingClientRect();
    var x = e.clientX;
    var y = e.clientY;
    return x >= rect.left + el.clientLeft &&
           x < rect.left + el.clientLeft + el.clientWidth &&
           y >= rect.top + el.clientTop &&
           y < rect.top + el.clientTop + el.clientHeight;
  }

  function getComputedStyle(el) {
    return el.ownerDocument.defaultView.getComputedStyle(el);
  }

  /**
   * Creates a new list element.
   * @param {Object=} opt_propertyBag Optional properties.
   * @constructor
   * @extends {HTMLUListElement}
   */
  var List = cr.ui.define('list');

  List.prototype = {
    __proto__: HTMLUListElement.prototype,

    /**
     * Measured size of list items. This is lazily calculated the first time it
     * is needed. Note that lead item is allowed to have a different height, to
     * accommodate lists where a single item at a time can be expanded to show
     * more detail.
     * @type {{height: number, marginTop: number, marginBottom:number,
     *     width: number, marginLeft: number, marginRight:number}}
     * @private
     */
    measured_: undefined,

    /**
     * Whether or not the list is autoexpanding. If true, the list resizes
     * its height to accomadate all children.
     * @type {boolean}
     * @private
     */
    autoExpands_: false,

    /**
     * Whether or not the rows on list have various heights. If true, all the
     * rows have the same fixed height. Otherwise, each row resizes its height
     * to accommodate all contents.
     * @type {boolean}
     * @private
     */
    fixedHeight_: true,

    /**
     * Whether or not the list view has a blank space below the last row.
     * @type {boolean}
     * @private
     */
    remainingSpace_: true,

    /**
     * Function used to create grid items.
     * @type {function(): !ListItem}
     * @private
     */
    itemConstructor_: cr.ui.ListItem,

    /**
     * Function used to create grid items.
     * @type {function(): !ListItem}
     */
    get itemConstructor() {
      return this.itemConstructor_;
    },
    set itemConstructor(func) {
      if (func != this.itemConstructor_) {
        this.itemConstructor_ = func;
        this.cachedItems_ = {};
        this.redraw();
      }
    },

    dataModel_: null,

    /**
     * The data model driving the list.
     * @type {ArrayDataModel}
     */
    set dataModel(dataModel) {
      if (this.dataModel_ != dataModel) {
        if (!this.boundHandleDataModelPermuted_) {
          this.boundHandleDataModelPermuted_ =
              this.handleDataModelPermuted_.bind(this);
          this.boundHandleDataModelChange_ =
              this.handleDataModelChange_.bind(this);
        }

        if (this.dataModel_) {
          this.dataModel_.removeEventListener(
              'permuted',
              this.boundHandleDataModelPermuted_);
          this.dataModel_.removeEventListener('change',
                                              this.boundHandleDataModelChange_);
        }

        this.dataModel_ = dataModel;

        this.cachedItems_ = {};
        this.cachedItemHeights_ = {};
        this.selectionModel.clear();
        if (dataModel)
          this.selectionModel.adjustLength(dataModel.length);

        if (this.dataModel_) {
          this.dataModel_.addEventListener(
              'permuted',
              this.boundHandleDataModelPermuted_);
          this.dataModel_.addEventListener('change',
                                           this.boundHandleDataModelChange_);
        }

        this.redraw();
      }
    },

    get dataModel() {
      return this.dataModel_;
    },


    /**
     * Cached item for measuring the default item size by measureItem().
     * @type {ListItem}
     */
    cachedMeasuredItem_: null,

    /**
     * The selection model to use.
     * @type {cr.ui.ListSelectionModel}
     */
    get selectionModel() {
      return this.selectionModel_;
    },
    set selectionModel(sm) {
      var oldSm = this.selectionModel_;
      if (oldSm == sm)
        return;

      if (!this.boundHandleOnChange_) {
        this.boundHandleOnChange_ = this.handleOnChange_.bind(this);
        this.boundHandleLeadChange_ = this.handleLeadChange_.bind(this);
      }

      if (oldSm) {
        oldSm.removeEventListener('change', this.boundHandleOnChange_);
        oldSm.removeEventListener('leadIndexChange',
                                  this.boundHandleLeadChange_);
      }

      this.selectionModel_ = sm;
      this.selectionController_ = this.createSelectionController(sm);

      if (sm) {
        sm.addEventListener('change', this.boundHandleOnChange_);
        sm.addEventListener('leadIndexChange', this.boundHandleLeadChange_);
      }
    },

    /**
     * Whether or not the list auto-expands.
     * @type {boolean}
     */
    get autoExpands() {
      return this.autoExpands_;
    },
    set autoExpands(autoExpands) {
      if (this.autoExpands_ == autoExpands)
        return;
      this.autoExpands_ = autoExpands;
      this.redraw();
    },

    /**
     * Whether or not the rows on list have various heights.
     * @type {boolean}
     */
    get fixedHeight() {
      return this.fixedHeight_;
    },
    set fixedHeight(fixedHeight) {
      if (this.fixedHeight_ == fixedHeight)
        return;
      this.fixedHeight_ = fixedHeight;
      this.redraw();
    },

    /**
     * Convenience alias for selectionModel.selectedItem
     * @type {*}
     */
    get selectedItem() {
      var dataModel = this.dataModel;
      if (dataModel) {
        var index = this.selectionModel.selectedIndex;
        if (index != -1)
          return dataModel.item(index);
      }
      return null;
    },
    set selectedItem(selectedItem) {
      var dataModel = this.dataModel;
      if (dataModel) {
        var index = this.dataModel.indexOf(selectedItem);
        this.selectionModel.selectedIndex = index;
      }
    },

    /**
     * Convenience alias for selectionModel.selectedItems
     * @type {!Array<*>}
     */
    get selectedItems() {
      var indexes = this.selectionModel.selectedIndexes;
      var dataModel = this.dataModel;
      if (dataModel) {
        return indexes.map(function(i) {
          return dataModel.item(i);
        });
      }
      return [];
    },

    /**
     * The HTML elements representing the items.
     * @type {HTMLCollection}
     */
    get items() {
      return Array.prototype.filter.call(this.children,
                                         this.isItem, this);
    },

    /**
     * Returns true if the child is a list item. Subclasses may override this
     * to filter out certain elements.
     * @param {Node} child Child of the list.
     * @return {boolean} True if a list item.
     */
    isItem: function(child) {
      return child.nodeType == Node.ELEMENT_NODE &&
             child != this.beforeFiller_ && child != this.afterFiller_;
    },

    batchCount_: 0,

    /**
     * When making a lot of updates to the list, the code could be wrapped in
     * the startBatchUpdates and finishBatchUpdates to increase performance. Be
     * sure that the code will not return without calling endBatchUpdates or the
     * list will not be correctly updated.
     */
    startBatchUpdates: function() {
      this.batchCount_++;
    },

    /**
     * See startBatchUpdates.
     */
    endBatchUpdates: function() {
      this.batchCount_--;
      if (this.batchCount_ == 0)
        this.redraw();
    },

    /**
     * Initializes the element.
     */
    decorate: function() {
      // Add fillers.
      this.beforeFiller_ = this.ownerDocument.createElement('div');
      this.afterFiller_ = this.ownerDocument.createElement('div');
      this.beforeFiller_.className = 'spacer';
      this.afterFiller_.className = 'spacer';
      this.textContent = '';
      this.appendChild(this.beforeFiller_);
      this.appendChild(this.afterFiller_);

      var length = this.dataModel ? this.dataModel.length : 0;
      this.selectionModel = new ListSelectionModel(length);

      this.addEventListener('dblclick', this.handleDoubleClickOrTap_);
      this.addEventListener('mousedown', this.handlePointerDownUp_);
      this.addEventListener('mouseup', this.handlePointerDownUp_);
      this.addEventListener('keydown', this.handleKeyDown);
      this.addEventListener('focus', this.handleElementFocus_, true);
      this.addEventListener('blur', this.handleElementBlur_, true);
      this.addEventListener('scroll', this.handleScroll.bind(this));
      this.setAttribute('role', 'list');

      this.touchHandler_ = new cr.ui.TouchHandler(this);
      this.addEventListener(cr.ui.TouchHandler.EventType.TOUCH_START,
                            this.handlePointerDownUp_);
      this.addEventListener(cr.ui.TouchHandler.EventType.TOUCH_END,
                            this.handlePointerDownUp_);
      this.addEventListener(cr.ui.TouchHandler.EventType.TAP,
                            this.handleDoubleClickOrTap_);
      this.touchHandler_.enable(false, false);
      // Make list focusable
      if (!this.hasAttribute('tabindex'))
        this.tabIndex = 0;

      // Try to get an unique id prefix from the id of this element or the
      // nearest ancestor with an id.
      var element = this;
      while (element && !element.id)
        element = element.parentElement;
      if (element && element.id)
        this.uniqueIdPrefix_ = element.id;
      else
        this.uniqueIdPrefix_ = 'list';

      // The next id suffix to use when giving each item an unique id.
      this.nextUniqueIdSuffix_ = 0;
    },

    /**
     * @param {ListItem=} item The list item to measure.
     * @return {number} The height of the given item. If the fixed height on CSS
     * is set by 'px', uses that value as height. Otherwise, measures the size.
     * @private
     */
    measureItemHeight_: function(item) {
      var height = item.style.height;
      // Use the fixed height if set it on CSS, to save a time of layout
      // calculation.
      if (height && height.substr(-2) == 'px')
        return parseInt(height.substr(0, height.length - 2));

      return this.measureItem(item).height;
    },

    /**
     * @return {number} The height of default item, measuring it if necessary.
     * @private
     */
    getDefaultItemHeight_: function() {
      return this.getDefaultItemSize_().height;
    },

    /**
     * @param {number} index The index of the item.
     * @return {number} The height of the item, measuring it if necessary.
     */
    getItemHeightByIndex_: function(index) {
      // If |this.fixedHeight_| is true, all the rows have same default height.
      if (this.fixedHeight_)
        return this.getDefaultItemHeight_();

      if (this.cachedItemHeights_[index])
        return this.cachedItemHeights_[index];

      var item = this.getListItemByIndex(index);
      if (item)
        return this.measureItemHeight_(item);

      return this.getDefaultItemHeight_();
    },

    /**
     * @return {{height: number, width: number}} The height and width
     *     of default item, measuring it if necessary.
     * @private
     */
    getDefaultItemSize_: function() {
      if (!this.measured_ || !this.measured_.height) {
        this.measured_ = this.measureItem();
      }
      return this.measured_;
    },

    /**
     * Creates an item (dataModel.item(0)) and measures its height. The item is
     * cached instead of creating a new one every time..
     * @param {ListItem=} opt_item The list item to use to do the measuring. If
     *     this is not provided an item will be created based on the first value
     *     in the model.
     * @return {{height: number, marginTop: number, marginBottom:number,
     *     width: number, marginLeft: number, marginRight:number}}
     *     The height and width of the item, taking
     *     margins into account, and the top, bottom, left and right margins
     *     themselves.
     */
    measureItem: function(opt_item) {
      var dataModel = this.dataModel;
      if (!dataModel || !dataModel.length)
        return 0;
      var item = opt_item || this.cachedMeasuredItem_ ||
          this.createItem(dataModel.item(0));
      if (!opt_item) {
        this.cachedMeasuredItem_ = item;
        this.appendChild(item);
      }

      var rect = item.getBoundingClientRect();
      var cs = getComputedStyle(item);
      var mt = parseFloat(cs.marginTop);
      var mb = parseFloat(cs.marginBottom);
      var ml = parseFloat(cs.marginLeft);
      var mr = parseFloat(cs.marginRight);
      var h = rect.height;
      var w = rect.width;
      var mh = 0;
      var mv = 0;

      // Handle margin collapsing.
      if (mt < 0 && mb < 0) {
        mv = Math.min(mt, mb);
      } else if (mt >= 0 && mb >= 0) {
        mv = Math.max(mt, mb);
      } else {
        mv = mt + mb;
      }
      h += mv;

      if (ml < 0 && mr < 0) {
        mh = Math.min(ml, mr);
      } else if (ml >= 0 && mr >= 0) {
        mh = Math.max(ml, mr);
      } else {
        mh = ml + mr;
      }
      w += mh;

      if (!opt_item)
        this.removeChild(item);
      return {
          height: Math.max(0, h),
          marginTop: mt, marginBottom: mb,
          width: Math.max(0, w),
          marginLeft: ml, marginRight: mr};
    },

    /**
     * Callback for the double click or TAP event.
     * @param {Event} e The mouse or TouchHandler event object.
     * @private
     */
    handleDoubleClickOrTap_: function(e) {
      if (this.disabled)
        return;

      var target = e.target;
      if (e.touchedElement)
        target = e.touchedElement;

      target = this.getListItemAncestor(target);
      if (target)
        this.activateItemAtIndex(this.getIndexOfListItem(target));
    },

    /**
     * Callback for mousedown, mouseup, TOUCH_START and TOUCH_END events.
     * @param {Event} e The mouse or TouchHandler event object.
     * @private
     */
    handlePointerDownUp_: function(e) {
      if (this.disabled)
        return;

      var target = e.target;
      if (e.touchedElement)
        target = e.touchedElement;

      // If the target was this element we need to make sure that the user did
      // not click on a border or a scrollbar.
      if (target == this) {
        if (inViewport(target, e))
          this.selectionController_.handlePointerDownUp(e, -1);
        return;
      }

      target = this.getListItemAncestor(target);

      // While target of mouse events always are ancestors TOUCH_END target
      // may be outside the list (for instance if it happens over a popup
      // menu). Just ignore it.
      if (!target)
        return;

      var index = this.getIndexOfListItem(target);
      this.selectionController_.handlePointerDownUp(e, index);
    },

    /**
     * Called when an element in the list is focused. Marks the list as having
     * a focused element, and dispatches an event if it didn't have focus.
     * @param {Event} e The focus event.
     * @private
     */
    handleElementFocus_: function(e) {
      if (!this.hasElementFocus)
        this.hasElementFocus = true;
    },

    /**
     * Called when an element in the list is blurred. If focus moves outside
     * the list, marks the list as no longer having focus and dispatches an
     * event.
     * @param {Event} e The blur event.
     * @private
     */
    handleElementBlur_: function(e) {
      // When the blur event happens we do not know who is getting focus so we
      // delay this a bit until we know if the new focus node is outside the
      // list.
      var list = this;
      var doc = e.target.ownerDocument;
      window.setTimeout(function() {
        var activeElement = doc.activeElement;
        if (!list.contains(activeElement))
          list.hasElementFocus = false;
      });
    },

    /**
     * Returns the list item element containing the given element, or null if
     * it doesn't belong to any list item element.
     * @param {HTMLElement} element The element.
     * @return {ListItem} The list item containing |element|, or null.
     */
    getListItemAncestor: function(element) {
      var container = element;
      while (container && container.parentNode != this) {
        container = container.parentNode;
      }
      return container;
    },

    /**
     * Handle a keydown event.
     * @param {Event} e The keydown event.
     * @return {boolean} Whether the key event was handled.
     */
    handleKeyDown: function(e) {
      if (this.disabled)
        return;

      return this.selectionController_.handleKeyDown(e);
    },

    scrollTopBefore_: 0,

    /**
     * Handle a scroll event.
     * @param {Event} e The scroll event.
     */
    handleScroll: function(e) {
      var scrollTop = this.scrollTop;
      if (scrollTop != this.scrollTopBefore_) {
        this.scrollTopBefore_ = scrollTop;
        this.redraw();
      }
    },

    /**
     * Callback from the selection model. We dispatch {@code change} events
     * when the selection changes.
     * @param {!cr.Event} e Event with change info.
     * @private
     */
    handleOnChange_: function(ce) {
      ce.changes.forEach(function(change) {
        var listItem = this.getListItemByIndex(change.index);
        if (listItem) {
          listItem.selected = change.selected;
          if (change.selected) {
            listItem.setAttribute('aria-posinset', change.index + 1);
            listItem.setAttribute('aria-setsize', this.dataModel.length);
            this.setAttribute('aria-activedescendant', listItem.id);
          } else {
            listItem.removeAttribute('aria-posinset');
            listItem.removeAttribute('aria-setsize');
          }
        }
      }, this);

      cr.dispatchSimpleEvent(this, 'change');
    },

    /**
     * Handles a change of the lead item from the selection model.
     * @param {Event} pe The property change event.
     * @private
     */
    handleLeadChange_: function(pe) {
      var element;
      if (pe.oldValue != -1) {
        if ((element = this.getListItemByIndex(pe.oldValue)))
          element.lead = false;
      }

      if (pe.newValue != -1) {
        if ((element = this.getListItemByIndex(pe.newValue)))
          element.lead = true;
        if (pe.oldValue != pe.newValue) {
          this.scrollIndexIntoView(pe.newValue);
          // If the lead item has a different height than other items, then we
          // may run into a problem that requires a second attempt to scroll
          // it into view. The first scroll attempt will trigger a redraw,
          // which will clear out the list and repopulate it with new items.
          // During the redraw, the list may shrink temporarily, which if the
          // lead item is the last item, will move the scrollTop up since it
          // cannot extend beyond the end of the list. (Sadly, being scrolled to
          // the bottom of the list is not "sticky.") So, we set a timeout to
          // rescroll the list after this all gets sorted out. This is perhaps
          // not the most elegant solution, but no others seem obvious.
          var self = this;
          window.setTimeout(function() {
            self.scrollIndexIntoView(pe.newValue);
          });
        }
      }
    },

    /**
     * This handles data model 'permuted' event.
     * this event is dispatched as a part of sort or splice.
     * We need to
     *  - adjust the cache.
     *  - adjust selection.
     *  - redraw. (called in this.endBatchUpdates())
     *  It is important that the cache adjustment happens before selection model
     *  adjustments.
     * @param {Event} e The 'permuted' event.
     */
    handleDataModelPermuted_: function(e) {
      var newCachedItems = {};
      for (var index in this.cachedItems_) {
        if (e.permutation[index] != -1) {
          var newIndex = e.permutation[index];
          newCachedItems[newIndex] = this.cachedItems_[index];
          newCachedItems[newIndex].listIndex = newIndex;
        }
      }
      this.cachedItems_ = newCachedItems;

      var newCachedItemHeights = {};
      for (var index in this.cachedItemHeights_) {
        if (e.permutation[index] != -1) {
          newCachedItemHeights[e.permutation[index]] =
              this.cachedItemHeights_[index];
        }
      }
      this.cachedItemHeights_ = newCachedItemHeights;

      this.startBatchUpdates();

      var sm = this.selectionModel;
      sm.adjustLength(e.newLength);
      sm.adjustToReordering(e.permutation);

      this.endBatchUpdates();
    },

    handleDataModelChange_: function(e) {
      delete this.cachedItems_[e.index];
      delete this.cachedItemHeights_[e.index];
      this.cachedMeasuredItem_ = null;

      if (e.index >= this.firstIndex_ &&
          (e.index < this.lastIndex_ || this.remainingSpace_)) {
        this.redraw();
      }
    },

    /**
     * @param {number} index The index of the item.
     * @return {number} The top position of the item inside the list.
     */
    getItemTop: function(index) {
      if (this.fixedHeight_) {
        var itemHeight = this.getDefaultItemHeight_();
        return index * itemHeight;
      } else {
        var top = 0;
        for (var i = 0; i < index; i++) {
          top += this.getItemHeightByIndex_(i);
        }
        return top;
      }
    },

    /**
     * @param {number} index The index of the item.
     * @return {number} The row of the item. May vary in the case
     *     of multiple columns.
     */
    getItemRow: function(index) {
      return index;
    },

    /**
     * @param {number} row The row.
     * @return {number} The index of the first item in the row.
     */
    getFirstItemInRow: function(row) {
      return row;
    },

    /**
     * Ensures that a given index is inside the viewport.
     * @param {number} index The index of the item to scroll into view.
     * @return {boolean} Whether any scrolling was needed.
     */
    scrollIndexIntoView: function(index) {
      var dataModel = this.dataModel;
      if (!dataModel || index < 0 || index >= dataModel.length)
        return false;

      var itemHeight = this.getItemHeightByIndex_(index);
      var scrollTop = this.scrollTop;
      var top = this.getItemTop(index);
      var clientHeight = this.clientHeight;

      var self = this;
      // Function to adjust the tops of viewport and row.
      function scrollToAdjustTop() {
          self.scrollTop = top;
          return true;
      };
      // Function to adjust the bottoms of viewport and row.
      function scrollToAdjustBottom() {
          var cs = getComputedStyle(self);
          var paddingY = parseInt(cs.paddingTop, 10) +
                         parseInt(cs.paddingBottom, 10);

          if (top + itemHeight > scrollTop + clientHeight - paddingY) {
            self.scrollTop = top + itemHeight - clientHeight + paddingY;
            return true;
          }
          return false;
      };

      // Check if the entire of given indexed row can be shown in the viewport.
      if (itemHeight <= clientHeight) {
        if (top < scrollTop)
          return scrollToAdjustTop();
        if (scrollTop + clientHeight < top + itemHeight)
          return scrollToAdjustBottom();
      } else {
        if (scrollTop < top)
          return scrollToAdjustTop();
        if (top + itemHeight < scrollTop + clientHeight)
          return scrollToAdjustBottom();
      }
      return false;
    },

    /**
     * @return {!ClientRect} The rect to use for the context menu.
     */
    getRectForContextMenu: function() {
      // TODO(arv): Add trait support so we can share more code between trees
      // and lists.
      var index = this.selectionModel.selectedIndex;
      var el = this.getListItemByIndex(index);
      if (el)
        return el.getBoundingClientRect();
      return this.getBoundingClientRect();
    },

    /**
     * Takes a value from the data model and finds the associated list item.
     * @param {*} value The value in the data model that we want to get the list
     *     item for.
     * @return {ListItem} The first found list item or null if not found.
     */
    getListItem: function(value) {
      var dataModel = this.dataModel;
      if (dataModel) {
        var index = dataModel.indexOf(value);
        return this.getListItemByIndex(index);
      }
      return null;
    },

    /**
     * Find the list item element at the given index.
     * @param {number} index The index of the list item to get.
     * @return {ListItem} The found list item or null if not found.
     */
    getListItemByIndex: function(index) {
      return this.cachedItems_[index] || null;
    },

    /**
     * Find the index of the given list item element.
     * @param {ListItem} item The list item to get the index of.
     * @return {number} The index of the list item, or -1 if not found.
     */
    getIndexOfListItem: function(item) {
      var index = item.listIndex;
      if (this.cachedItems_[index] == item) {
        return index;
      }
      return -1;
    },

    /**
     * Creates a new list item.
     * @param {*} value The value to use for the item.
     * @return {!ListItem} The newly created list item.
     */
    createItem: function(value) {
      var item = new this.itemConstructor_(value);
      item.label = value;
      item.id = this.uniqueIdPrefix_ + '-' + this.nextUniqueIdSuffix_++;
      if (typeof item.decorate == 'function')
        item.decorate();
      return item;
    },

    /**
     * Creates the selection controller to use internally.
     * @param {cr.ui.ListSelectionModel} sm The underlying selection model.
     * @return {!cr.ui.ListSelectionController} The newly created selection
     *     controller.
     */
    createSelectionController: function(sm) {
      return new ListSelectionController(sm);
    },

    /**
     * Return the heights (in pixels) of the top of the given item index within
     * the list, and the height of the given item itself, accounting for the
     * possibility that the lead item may be a different height.
     * @param {number} index The index to find the top height of.
     * @return {{top: number, height: number}} The heights for the given index.
     * @private
     */
    getHeightsForIndex_: function(index) {
      var itemHeight = this.getItemHeightByIndex_(index);
      var top = this.getItemTop(index);
      return {top: top, height: itemHeight};
    },

    /**
     * Find the index of the list item containing the given y offset (measured
     * in pixels from the top) within the list. In the case of multiple columns,
     * returns the first index in the row.
     * @param {number} offset The y offset in pixels to get the index of.
     * @return {number} The index of the list item. Returns the list size if
     *     given offset exceeds the height of list.
     * @private
     */
    getIndexForListOffset_: function(offset) {
      var itemHeight = this.getDefaultItemHeight_();
      if (!itemHeight)
        return this.dataModel.length;

      if (this.fixedHeight_)
        return this.getFirstItemInRow(Math.floor(offset / itemHeight));

      // If offset exceeds the height of list.
      var lastHeight = 0;
      if (this.dataModel.length) {
        var h = this.getHeightsForIndex_(this.dataModel.length - 1);
        lastHeight = h.top + h.height;
      }
      if (lastHeight < offset)
        return this.dataModel.length;

      // Estimates index.
      var estimatedIndex = Math.min(Math.floor(offset / itemHeight),
                                    this.dataModel.length - 1);
      var isIncrementing = this.getItemTop(estimatedIndex) < offset;

      // Searchs the correct index.
      do {
        var heights = this.getHeightsForIndex_(estimatedIndex);
        var top = heights.top;
        var height = heights.height;

        if (top <= offset && offset <= (top + height))
          break;

        isIncrementing ? ++estimatedIndex : --estimatedIndex;
      } while (0 < estimatedIndex && estimatedIndex < this.dataModel.length);

      return estimatedIndex;
    },

    /**
     * Return the number of items that occupy the range of heights between the
     * top of the start item and the end offset.
     * @param {number} startIndex The index of the first visible item.
     * @param {number} endOffset The y offset in pixels of the end of the list.
     * @return {number} The number of list items visible.
     * @private
     */
    countItemsInRange_: function(startIndex, endOffset) {
      var endIndex = this.getIndexForListOffset_(endOffset);
      return endIndex - startIndex + 1;
    },

    /**
     * Calculates the number of items fitting in the given viewport.
     * @param {number} scrollTop The scroll top position.
     * @param {number} clientHeight The height of viewport.
     * @return {{first: number, length: number, last: number}} The index of
     *     first item in view port, The number of items, The item past the last.
     */
    getItemsInViewPort: function(scrollTop, clientHeight) {
      if (this.autoExpands_) {
        return {
          first: 0,
          length: this.dataModel.length,
          last: this.dataModel.length};
      } else {
        var firstIndex = this.getIndexForListOffset_(scrollTop);
        var lastIndex = this.getIndexForListOffset_(scrollTop + clientHeight);

        return {
          first: firstIndex,
          length: lastIndex - firstIndex + 1,
          last: lastIndex + 1};
      }
    },

    /**
     * Merges list items currently existing in the list with items in the range
     * [firstIndex, lastIndex). Removes or adds items if needed.
     * Doesn't delete {@code this.pinnedItem_} if it is present (instead hides
     * it if it is out of the range). Adds items to {@code newCachedItems}.
     * @param {number} firstIndex The index of first item, inclusively.
     * @param {number} lastIndex The index of last item, exclusively.
     * @param {Object.<string, ListItem>} cachedItems Old items cache.
     * @param {Object.<string, ListItem>} newCachedItems New items cache.
     */
    mergeItems: function(firstIndex, lastIndex, cachedItems, newCachedItems) {
      var self = this;
      var dataModel = this.dataModel;
      var currentIndex = firstIndex;

      function insert() {
        var dataItem = dataModel.item(currentIndex);
        var newItem = cachedItems[currentIndex] || self.createItem(dataItem);
        newItem.listIndex = currentIndex;
        newCachedItems[currentIndex] = newItem;
        self.insertBefore(newItem, item);
        currentIndex++;
      }

      function remove() {
        var next = item.nextSibling;
        if (item != self.pinnedItem_)
          self.removeChild(item);
        item = next;
      }

      for (var item = this.beforeFiller_.nextSibling;
           item != this.afterFiller_ && currentIndex < lastIndex;) {
        if (!this.isItem(item)) {
          item = item.nextSibling;
          continue;
        }

        var index = item.listIndex;
        if (cachedItems[index] != item || index < currentIndex) {
          remove();
        } else if (index == currentIndex) {
          newCachedItems[currentIndex] = item;
          item = item.nextSibling;
          currentIndex++;
        } else {  // index > currentIndex
          insert();
        }
      }

      while (item != this.afterFiller_) {
        if (this.isItem(item))
          remove();
        else
          item = item.nextSibling;
      }

      if (this.pinnedItem_) {
        var index = this.pinnedItem_.listIndex;
        this.pinnedItem_.hidden = index < firstIndex || index >= lastIndex;
        newCachedItems[index] = this.pinnedItem_;
        if (index >= lastIndex)
          item = this.pinnedItem_;  // Insert new items before this one.
      }

      while (currentIndex < lastIndex)
        insert();
    },

    /**
     * Ensures that all the item sizes in the list have been already cached.
     */
    ensureAllItemSizesInCache: function() {
      var measuringIndexes = [];
      var isElementAppended = [];
      for (var y = 0; y < this.dataModel.length; y++) {
        if (!this.cachedItemHeights_[y]) {
          measuringIndexes.push(y);
          isElementAppended.push(false);
        }
      }

      var measuringItems = [];
      // Adds temporary elements.
      for (var y = 0; y < measuringIndexes.length; y++) {
        var index = measuringIndexes[y];
        var dataItem = this.dataModel.item(index);
        var listItem = this.cachedItems_[index] || this.createItem(dataItem);
        listItem.listIndex = index;

        // If |listItems| is not on the list, apppends it to the list and sets
        // the flag.
        if (!listItem.parentNode) {
          this.appendChild(listItem);
          isElementAppended[y] = true;
        }

        this.cachedItems_[index] = listItem;
        measuringItems.push(listItem);
      }

      // All mesurings must be placed after adding all the elements, to prevent
      // performance reducing.
      for (var y = 0; y < measuringIndexes.length; y++) {
        var index = measuringIndexes[y];
        this.cachedItemHeights_[index] =
            this.measureItemHeight_(measuringItems[y]);
      }

      // Removes all the temprary elements.
      for (var y = 0; y < measuringIndexes.length; y++) {
        // If the list item has been appended above, removes it.
        if (isElementAppended[y])
          this.removeChild(measuringItems[y]);
      }
    },

    /**
     * Returns the height of after filler in the list.
     * @param {number} lastIndex The index of item past the last in viewport.
     * @param {number} itemHeight The height of the item.
     * @return {number} The height of after filler.
     */
    getAfterFillerHeight: function(lastIndex) {
      if (this.fixedHeight_) {
        var itemHeight = this.getDefaultItemHeight_();
        return (this.dataModel.length - lastIndex) * itemHeight;
      }

      var height = 0;
      for (var i = lastIndex; i < this.dataModel.length; i++)
        height += this.getItemHeightByIndex_(i);
      return height;
    },

    /**
     * Redraws the viewport.
     */
    redraw: function() {
      if (this.batchCount_ != 0)
        return;

      var dataModel = this.dataModel;
      if (!dataModel || !this.autoExpands_ && this.clientHeight == 0) {
        this.cachedItems_ = {};
        this.firstIndex_ = 0;
        this.lastIndex_ = 0;
        this.remainingSpace_ = true;
        this.mergeItems(0, 0, {}, {});
        return;
      }

      // Save the previous positions before any manipulation of elements.
      var scrollTop = this.scrollTop;
      var clientHeight = this.clientHeight;

      // Store all the item sizes into the cache in advance, to prevent
      // interleave measuring with mutating dom.
      if (!this.fixedHeight_)
        this.ensureAllItemSizesInCache();

      // We cache the list items since creating the DOM nodes is the most
      // expensive part of redrawing.
      var cachedItems = this.cachedItems_ || {};
      var newCachedItems = {};

      var autoExpands = this.autoExpands_;

      var itemsInViewPort = this.getItemsInViewPort(scrollTop, clientHeight);
      // Draws the hidden rows just above/below the viewport to prevent
      // flashing in scroll.
      var firstIndex = Math.max(0, itemsInViewPort.first - 1);
      var lastIndex = Math.min(itemsInViewPort.last + 1, dataModel.length);

      var beforeFillerHeight =
          this.autoExpands ? 0 : this.getItemTop(firstIndex);
      var afterFillerHeight =
          this.autoExpands ? 0 : this.getAfterFillerHeight(lastIndex);

      this.beforeFiller_.style.height = beforeFillerHeight + 'px';

      var sm = this.selectionModel;
      var leadIndex = sm.leadIndex;

      if (this.pinnedItem_ &&
          this.pinnedItem_ != cachedItems[leadIndex]) {
        if (this.pinnedItem_.hidden)
          this.removeChild(this.pinnedItem_);
        this.pinnedItem_ = undefined;
      }

      this.mergeItems(firstIndex, lastIndex, cachedItems, newCachedItems);

      if (!this.pinnedItem_ && newCachedItems[leadIndex] &&
          newCachedItems[leadIndex].parentNode == this) {
        this.pinnedItem_ = newCachedItems[leadIndex];
      }

      this.afterFiller_.style.height = afterFillerHeight + 'px';

      // We don't set the lead or selected properties until after adding all
      // items, in case they force relayout in response to these events.
      var listItem = null;
      if (leadIndex != -1 && newCachedItems[leadIndex])
        newCachedItems[leadIndex].lead = true;
      for (var y = firstIndex; y < lastIndex; y++) {
        if (sm.getIndexSelected(y))
          newCachedItems[y].selected = true;
        else if (y != leadIndex)
          listItem = newCachedItems[y];
      }

      this.firstIndex_ = firstIndex;
      this.lastIndex_ = lastIndex;

      this.remainingSpace_ = itemsInViewPort.last > dataModel.length;
      this.cachedItems_ = newCachedItems;

      // Mesurings must be placed after adding all the elements, to prevent
      // performance reducing.
      if (!this.fixedHeight_) {
        for (var y = firstIndex; y < lastIndex; y++)
          this.cachedItemHeights_[y] =
              this.measureItemHeight_(newCachedItems[y]);
      }

      // Measure again in case the item height has changed due to a page zoom.
      //
      // The measure above is only done the first time but this measure is done
      // after every redraw. It is done in a timeout so it will not trigger
      // a reflow (which made the redraw speed 3 times slower on my system).
      // By using a timeout the measuring will happen later when there is no
      // need for a reflow.
      if (listItem && this.fixedHeight_) {
        var list = this;
        window.setTimeout(function() {
          if (listItem.parentNode == list) {
            list.measured_ = list.measureItem(listItem);
          }
        });
      }
    },

    /**
     * Restore the lead item that is present in the list but may be updated
     * in the data model (supposed to be used inside a batch update). Usually
     * such an item would be recreated in the redraw method. If reinsertion
     * is undesirable (for instance to prevent losing focus) the item may be
     * updated and restored. Assumed the listItem relates to the same data item
     * as the lead item in the begin of the batch update.
     *
     * @param {ListItem} leadItem Already existing lead item.
     */
    restoreLeadItem: function(leadItem) {
      delete this.cachedItems_[leadItem.listIndex];

      leadItem.listIndex = this.selectionModel.leadIndex;
      this.pinnedItem_ = this.cachedItems_[leadItem.listIndex] = leadItem;
    },

    /**
     * Invalidates list by removing cached items.
     */
    invalidate: function() {
      this.cachedItems_ = {};
      this.cachedItemSized_ = {};
    },

    /**
     * Redraws a single item.
     * @param {number} index The row index to redraw.
     */
    redrawItem: function(index) {
      if (index >= this.firstIndex_ &&
          (index < this.lastIndex_ || this.remainingSpace_)) {
        delete this.cachedItems_[index];
        this.redraw();
      }
    },

    /**
     * Called when a list item is activated, currently only by a double click
     * event.
     * @param {number} index The index of the activated item.
     */
    activateItemAtIndex: function(index) {
    },

    /**
     * Returns a ListItem for the leadIndex. If the item isn't present in the
     * list creates it and inserts to the list (may be invisible if it's out of
     * the visible range).
     *
     * Item returned from this method won't be removed until it remains a lead
     * item or til the data model changes (unlike other items that could be
     * removed when they go out of the visible range).
     *
     * @return {cr.ui.ListItem} The lead item for the list.
     */
    ensureLeadItemExists: function() {
      var index = this.selectionModel.leadIndex;
      if (index < 0)
        return null;
      var cachedItems = this.cachedItems_ || {};

      var item = cachedItems[index] ||
                 this.createItem(this.dataModel.item(index));
      if (this.pinnedItem_ != item && this.pinnedItem_ &&
          this.pinnedItem_.hidden) {
        this.removeChild(this.pinnedItem_);
      }
      this.pinnedItem_ = item;
      cachedItems[index] = item;
      item.listIndex = index;
      if (item.parentNode == this)
        return item;

      if (this.batchCount_ != 0)
        item.hidden = true;

      // Item will get to the right place in redraw. Choose place to insert
      // reducing items reinsertion.
      if (index <= this.firstIndex_)
        this.insertBefore(item, this.beforeFiller_.nextSibling);
      else
        this.insertBefore(item, this.afterFiller_);
      this.redraw();
      return item;
    },
  };

  cr.defineProperty(List, 'disabled', cr.PropertyKind.BOOL_ATTR);

  /**
   * Whether the list or one of its descendents has focus. This is necessary
   * because list items can contain controls that can be focused, and for some
   * purposes (e.g., styling), the list can still be conceptually focused at
   * that point even though it doesn't actually have the page focus.
   */
  cr.defineProperty(List, 'hasElementFocus', cr.PropertyKind.BOOL_ATTR);

  return {
    List: List
  };
});
