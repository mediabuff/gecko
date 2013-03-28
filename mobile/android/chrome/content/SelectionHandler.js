/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

var SelectionHandler = {
  HANDLE_TYPE_START: "START",
  HANDLE_TYPE_MIDDLE: "MIDDLE",
  HANDLE_TYPE_END: "END",

  TYPE_NONE: 0,
  TYPE_CURSOR: 1,
  TYPE_SELECTION: 2,

  // Keeps track of data about the dimensions of the selection. Coordinates
  // stored here are relative to the _contentWindow window.
  _cache: null,
  _activeType: 0, // TYPE_NONE

  // The window that holds the selection (can be a sub-frame)
  get _contentWindow() {
    if (this._contentWindowRef)
      return this._contentWindowRef.get();
    return null;
  },

  set _contentWindow(aContentWindow) {
    this._contentWindowRef = Cu.getWeakReference(aContentWindow);
  },

  get _targetElement() {
    if (this._targetElementRef)
      return this._targetElementRef.get();
    return null;
  },

  set _targetElement(aTargetElement) {
    this._targetElementRef = Cu.getWeakReference(aTargetElement);
  },

  get _domWinUtils() {
    return BrowserApp.selectedBrowser.contentWindow.QueryInterface(Ci.nsIInterfaceRequestor).
                                                    getInterface(Ci.nsIDOMWindowUtils);
  },

  _isRTL: false,

  _addObservers: function sh_addObservers() {
    Services.obs.addObserver(this, "Gesture:SingleTap", false);
    Services.obs.addObserver(this, "Window:Resize", false);
    Services.obs.addObserver(this, "Tab:Selected", false);
    Services.obs.addObserver(this, "after-viewport-change", false);
    Services.obs.addObserver(this, "TextSelection:Move", false);
    Services.obs.addObserver(this, "TextSelection:Position", false);
    BrowserApp.deck.addEventListener("compositionend", this, false);
  },

  _removeObservers: function sh_removeObservers() {
    Services.obs.removeObserver(this, "Gesture:SingleTap");
    Services.obs.removeObserver(this, "Window:Resize");
    Services.obs.removeObserver(this, "Tab:Selected");
    Services.obs.removeObserver(this, "after-viewport-change");
    Services.obs.removeObserver(this, "TextSelection:Move");
    Services.obs.removeObserver(this, "TextSelection:Position");
    BrowserApp.deck.removeEventListener("compositionend", this);
  },

  observe: function sh_observe(aSubject, aTopic, aData) {
    switch (aTopic) {
      case "Gesture:SingleTap": {
        if (this._activeType == this.TYPE_SELECTION) {
          let data = JSON.parse(aData);
          if (this._pointInSelection(data.x, data.y))
            this.copySelection();
          else
            this._closeSelection();
        }
        break;
      }
      case "Tab:Selected":
        this._closeSelection();
        break;
  
      case "Window:Resize": {
        if (this._activeType == this.TYPE_SELECTION) {
          // Knowing when the page is done drawing is hard, so let's just cancel
          // the selection when the window changes. We should fix this later.
          this._closeSelection();
        }
        break;
      }
      case "after-viewport-change": {
        if (this._activeType == this.TYPE_SELECTION) {
          // Update the cache after the viewport changes (e.g. panning, zooming).
          this._updateCacheForSelection();
        }
        break;
      }
      case "TextSelection:Move": {
        let data = JSON.parse(aData);
        if (this._activeType == this.TYPE_SELECTION)
          this._moveSelection(data.handleType == this.HANDLE_TYPE_START, data.x, data.y);
        else if (this._activeType == this.TYPE_CURSOR) {
          // Send a click event to the text box, which positions the caret
          this._sendMouseEvents(data.x, data.y);

          // Move the handle directly under the caret
          this._positionHandles();
        }
        break;
      }
      case "TextSelection:Position": {
        if (this._activeType == this.TYPE_SELECTION) {
          let data = JSON.parse(aData);

          // Reverse the handles if necessary.
          let selectionReversed = this._updateCacheForSelection(data.handleType == this.HANDLE_TYPE_START);
          if (selectionReversed) {
            // Re-send mouse events to update the selection corresponding to the new handles.
            if (this._isRTL) {
              this._sendMouseEvents(this._cache.end.x, this._cache.end.y, false);
              this._sendMouseEvents(this._cache.start.x, this._cache.start.y, true);
            } else {
              this._sendMouseEvents(this._cache.start.x, this._cache.start.y, false);
              this._sendMouseEvents(this._cache.end.x, this._cache.end.y, true);
            }
          }

          // Position the handles to align with the edges of the selection.
          this._positionHandles();
        } else if (this._activeType == this.TYPE_CURSOR) {
          this._positionHandles();
        }
        break;
      }
    }
  },

  handleEvent: function sh_handleEvent(aEvent) {
    switch (aEvent.type) {
      case "pagehide":
      // We only add keydown and blur listeners for TYPE_CURSOR
      case "keydown":
      case "blur":
        this._closeSelection();
        break;

      case "compositionend":
        if (this._activeType == this.TYPE_CURSOR) {
          this._closeSelection();
        }
        break;
    }
  },

  _ignoreCollapsedSelection: false,

  notifySelectionChanged: function sh_notifySelectionChanged(aDoc, aSel, aReason) {
    if (aSel.isCollapsed) {
      // Bail if we're ignoring events for a collapsed selection.
      if (this._ignoreCollapsedSelection)
        return;

      // If the selection is collapsed because of one of the mouse events we
      // sent while moving the handle, don't get rid of the selection handles.
      if (aReason & Ci.nsISelectionListener.MOUSEDOWN_REASON) {
        this._ignoreCollapsedSelection = true;
        return;
      }

      // Otherwise, we do want to close the selection.
      this._closeSelection();
    }

    this._ignoreCollapsedSelection = false;
  },

  /** Returns true if the provided element can be selected in text selection, false otherwise. */
  canSelect: function sh_canSelect(aElement) {
    return !(aElement instanceof Ci.nsIDOMHTMLButtonElement ||
             aElement instanceof Ci.nsIDOMHTMLEmbedElement ||
             aElement instanceof Ci.nsIDOMHTMLImageElement ||
             aElement instanceof Ci.nsIDOMHTMLMediaElement) &&
             aElement.style.MozUserSelect != 'none';
  },

  // aX/aY are in top-level window browser coordinates
  startSelection: function sh_startSelection(aElement, aX, aY) {
    // Clear out any existing selection
    this._closeSelection();

    this._contentWindow = aElement.ownerDocument.defaultView;
    this._targetElement = aElement;

    this._addObservers();
    this._contentWindow.addEventListener("pagehide", this, false);
    this._isRTL = (this._contentWindow.getComputedStyle(aElement, "").direction == "rtl");

    // Remove any previous selected or created ranges. Tapping anywhere on a
    // page will create an empty range.
    let selection = this._getSelection();
    selection.removeAllRanges();

    // Position the caret using a fake mouse click sent to the top-level window
    this._sendMouseEvents(aX, aY, false);

    try {
      let selectionController = this._getSelectionController();

      // Select the word nearest the caret
      selectionController.wordMove(false, false);

      // Move forward in LTR, backward in RTL
      selectionController.wordMove(!this._isRTL, true);
    } catch(e) {
      // If we couldn't select the word at the given point, bail
      this._closeSelection();
      return;
    }

    // If there isn't an appropriate selection, bail
    if (!selection.rangeCount || !selection.getRangeAt(0) || !selection.toString().trim().length) {
      selection.collapseToStart();
      this._closeSelection();
      return;
    }

    // Add a listener to end the selection if it's removed programatically
    selection.QueryInterface(Ci.nsISelectionPrivate).addSelectionListener(this);

    // Initialize the cache
    this._cache = { start: {}, end: {}};
    this._updateCacheForSelection();

    this._activeType = this.TYPE_SELECTION;
    this._positionHandles();

    sendMessageToJava({
      type: "TextSelection:ShowHandles",
      handles: [this.HANDLE_TYPE_START, this.HANDLE_TYPE_END]
    });

    if (aElement instanceof Ci.nsIDOMNSEditableElement)
      aElement.focus();
  },

  _getSelection: function sh_getSelection() {
    if (this._targetElement instanceof Ci.nsIDOMNSEditableElement)
      return this._targetElement.QueryInterface(Ci.nsIDOMNSEditableElement).editor.selection;
    else
      return this._contentWindow.getSelection();
  },

  _getSelectedText: function sh_getSelectedText() {
    let selection = this._getSelection();
    if (selection)
      return selection.toString().trim();
    return "";
  },

  _getSelectionController: function sh_getSelectionController() {
    if (this._targetElement instanceof Ci.nsIDOMNSEditableElement)
      return this._targetElement.QueryInterface(Ci.nsIDOMNSEditableElement).editor.selectionController;
    else
      return this._contentWindow.QueryInterface(Ci.nsIInterfaceRequestor).
                                 getInterface(Ci.nsIWebNavigation).
                                 QueryInterface(Ci.nsIInterfaceRequestor).
                                 getInterface(Ci.nsISelectionDisplay).
                                 QueryInterface(Ci.nsISelectionController);
  },

  // Used by the contextmenu "matches" functions in ClipboardHelper
  shouldShowContextMenu: function sh_shouldShowContextMenu(aX, aY) {
    return (this._activeType == this.TYPE_SELECTION) && this._pointInSelection(aX, aY);
  },

  selectAll: function sh_selectAll(aElement, aX, aY) {
    if (this._activeType != this.TYPE_SELECTION)
      this.startSelection(aElement, aX, aY);

    let selectionController = this._getSelectionController();
    selectionController.selectAll();
    this._updateCacheForSelection();
    this._positionHandles();
  },

  // Moves the ends of the selection in the page. aX/aY are in top-level window
  // browser coordinates.
  _moveSelection: function sh_moveSelection(aIsStartHandle, aX, aY) {
    // Update the handle position as it's dragged.
    if (aIsStartHandle) {
      this._cache.start.x = aX;
      this._cache.start.y = aY;
    } else {
      this._cache.end.x = aX;
      this._cache.end.y = aY;
    }

    // The handles work the same on both LTR and RTL pages, but the underlying selection
    // works differently, so we need to reverse how we send mouse events on RTL pages.
    if (this._isRTL) {
      // Position the caret at the end handle using a fake mouse click
      if (!aIsStartHandle)
        this._sendMouseEvents(this._cache.end.x, this._cache.end.y, false);

      // Selects text between the carat and the start handle using a fake shift+click
      this._sendMouseEvents(this._cache.start.x, this._cache.start.y, true);
    } else {
      // Position the caret at the start handle using a fake mouse click
      if (aIsStartHandle)
        this._sendMouseEvents(this._cache.start.x, this._cache.start.y, false);

      // Selects text between the carat and the end handle using a fake shift+click
      this._sendMouseEvents( this._cache.end.x, this._cache.end.y, true);
    }
  },

  _sendMouseEvents: function sh_sendMouseEvents(aX, aY, useShift) {
    // If we're positioning a cursor in an input field, make sure the handle
    // stays within the bounds of the field
    if (this._activeType == this.TYPE_CURSOR) {
      // Get rect of text inside element
      let range = document.createRange();
      range.selectNodeContents(this._targetElement.QueryInterface(Ci.nsIDOMNSEditableElement).editor.rootElement);
      let textBounds = range.getBoundingClientRect();

      // Get rect of editor
      let editorBounds = this._domWinUtils.sendQueryContentEvent(this._domWinUtils.QUERY_EDITOR_RECT, 0, 0, 0, 0);
      let editorRect = new Rect(editorBounds.left, editorBounds.top, editorBounds.width, editorBounds.height);

      // Use intersection of the text rect and the editor rect
      let rect = new Rect(textBounds.left, textBounds.top, textBounds.width, textBounds.height);
      rect.restrictTo(editorRect);

      // Clamp vertically and scroll if handle is at bounds. The top and bottom
      // must be restricted by an additional pixel since clicking on the top
      // edge of an input field moves the cursor to the beginning of that
      // field's text (and clicking the bottom moves the cursor to the end).
      if (aY < rect.y + 1) {
        aY = rect.y + 1;
        this._getSelectionController().scrollLine(false);
      } else if (aY > rect.y + rect.height - 1) {
        aY = rect.y + rect.height - 1;
        this._getSelectionController().scrollLine(true);
      }

      // Clamp horizontally and scroll if handle is at bounds
      if (aX < rect.x) {
        aX = rect.x;
        this._getSelectionController().scrollCharacter(false);
      } else if (aX > rect.x + rect.width) {
        aX = rect.x + rect.width;
        this._getSelectionController().scrollCharacter(true);
      }
    } else if (this._activeType == this.TYPE_SELECTION) {
      // Send mouse event 1px too high to prevent selection from entering the line below where it should be
      aY -= 1;
    }

    this._domWinUtils.sendMouseEventToWindow("mousedown", aX, aY, 0, 0, useShift ? Ci.nsIDOMNSEvent.SHIFT_MASK : 0, true);
    this._domWinUtils.sendMouseEventToWindow("mouseup", aX, aY, 0, 0, useShift ? Ci.nsIDOMNSEvent.SHIFT_MASK : 0, true);
  },

  copySelection: function sh_copySelection() {
    let selectedText = this._getSelectedText();
    if (selectedText.length) {
      let clipboard = Cc["@mozilla.org/widget/clipboardhelper;1"].getService(Ci.nsIClipboardHelper);
      clipboard.copyString(selectedText, this._contentWindow.document);
      NativeWindow.toast.show(Strings.browser.GetStringFromName("selectionHelper.textCopied"), "short");
    }
    this._closeSelection();
  },

  shareSelection: function sh_shareSelection() {
    let selectedText = this._getSelectedText();
    if (selectedText.length) {
      sendMessageToJava({
        type: "Share:Text",
        text: selectedText
      });
    }
    this._closeSelection();
  },

  /*
   * Shuts SelectionHandler down.
   */
  _closeSelection: function sh_closeSelection() {
    // Bail if there's no active selection
    if (this._activeType == this.TYPE_NONE)
      return;

    if (this._activeType == this.TYPE_SELECTION) {
      let selection = this._getSelection();
      if (selection) {
        // Remove the listener before calling removeAllRanges() to avoid
        // recursively notifying the listener.
        selection.QueryInterface(Ci.nsISelectionPrivate).removeSelectionListener(this);
        selection.removeAllRanges();
      }
    }

    this._activeType = this.TYPE_NONE;

    sendMessageToJava({ type: "TextSelection:HideHandles" });

    this._removeObservers();
    this._contentWindow.removeEventListener("pagehide", this, false);
    this._contentWindow.removeEventListener("keydown", this, false);
    this._contentWindow.removeEventListener("blur", this, true);
    this._contentWindow = null;
    this._targetElement = null;
    this._isRTL = false;
    this._cache = null;
  },

  _getViewOffset: function sh_getViewOffset() {
    let offset = { x: 0, y: 0 };
    let win = this._contentWindow;

    // Recursively look through frames to compute the total position offset.
    while (win.frameElement) {
      let rect = win.frameElement.getBoundingClientRect();
      offset.x += rect.left;
      offset.y += rect.top;

      win = win.parent;
    }

    return offset;
  },

  _pointInSelection: function sh_pointInSelection(aX, aY) {
    let offset = this._getViewOffset();
    let rangeRect = this._getSelection().getRangeAt(0).getBoundingClientRect();
    let radius = ElementTouchHelper.getTouchRadius();
    return (aX - offset.x > rangeRect.left - radius.left &&
            aX - offset.x < rangeRect.right + radius.right &&
            aY - offset.y > rangeRect.top - radius.top &&
            aY - offset.y < rangeRect.bottom + radius.bottom);
  },

  // Returns true if the selection has been reversed. Takes optional aIsStartHandle
  // param to decide whether the selection has been reversed.
  _updateCacheForSelection: function sh_updateCacheForSelection(aIsStartHandle) {
    let selection = this._getSelection();
    let rects = selection.getRangeAt(0).getClientRects();
    let start = { x: this._isRTL ? rects[0].right : rects[0].left, y: rects[0].bottom };
    let end = { x: this._isRTL ? rects[rects.length - 1].left : rects[rects.length - 1].right, y: rects[rects.length - 1].bottom };

    let selectionReversed = false;
    if (this._cache.start) {
      // If the end moved past the old end, but we're dragging the start handle, then that handle should become the end handle (and vice versa)
      selectionReversed = (aIsStartHandle && (end.y > this._cache.end.y || (end.y == this._cache.end.y && end.x > this._cache.end.x))) ||
                          (!aIsStartHandle && (start.y < this._cache.start.y || (start.y == this._cache.start.y && start.x < this._cache.start.x)));
    }

    this._cache.start = start;
    this._cache.end = end;

    return selectionReversed;
  },

  showThumb: function sh_showThumb(aElement) {
    if (!aElement)
      return;

    // Get the element's view
    this._contentWindow = aElement.ownerDocument.defaultView;
    this._targetElement = aElement;

    this._addObservers();
    this._contentWindow.addEventListener("pagehide", this, false);
    this._contentWindow.addEventListener("keydown", this, false);
    this._contentWindow.addEventListener("blur", this, true);

    this._activeType = this.TYPE_CURSOR;
    this._positionHandles();

    sendMessageToJava({
      type: "TextSelection:ShowHandles",
      handles: [this.HANDLE_TYPE_MIDDLE]
    });
  },

  _positionHandles: function sh_positionHandles() {
    let scrollX = {}, scrollY = {};
    this._contentWindow.top.QueryInterface(Ci.nsIInterfaceRequestor).
                            getInterface(Ci.nsIDOMWindowUtils).getScrollXY(false, scrollX, scrollY);

    // the checkHidden function tests to see if the given point is hidden inside an
    // iframe/subdocument. this is so that if we select some text inside an iframe and
    // scroll the iframe so the selection is out of view, we hide the handles rather
    // than having them float on top of the main page content.
    let checkHidden = function(x, y) {
      return false;
    };
    if (this._contentWindow.frameElement) {
      let bounds = this._contentWindow.frameElement.getBoundingClientRect();
      checkHidden = function(x, y) {
        return x < 0 || y < 0 || x > bounds.width || y > bounds.height;
      }
    }

    let positions = null;
    if (this._activeType == this.TYPE_CURSOR) {
      // The left and top properties returned are relative to the client area
      // of the window, so we don't need to account for a sub-frame offset.
      let cursor = this._domWinUtils.sendQueryContentEvent(this._domWinUtils.QUERY_CARET_RECT, this._targetElement.selectionEnd, 0, 0, 0);
      let x = cursor.left;
      let y = cursor.top + cursor.height;
      positions = [{ handle: this.HANDLE_TYPE_MIDDLE,
                     left: x + scrollX.value,
                     top: y + scrollY.value,
                     hidden: checkHidden(x, y) }];
    } else {
      let sx = this._cache.start.x;
      let sy = this._cache.start.y;
      let ex = this._cache.end.x;
      let ey = this._cache.end.y;

      // Translate coordinates to account for selections in sub-frames. We can't cache
      // this because the top-level page may have scrolled since selection started.
      let offset = this._getViewOffset();

      positions = [{ handle: this.HANDLE_TYPE_START,
                     left: sx + offset.x + scrollX.value,
                     top: sy + offset.y + scrollY.value,
                     hidden: checkHidden(sx, sy) },
                   { handle: this.HANDLE_TYPE_END,
                     left: ex + offset.x + scrollX.value,
                     top: ey + offset.y + scrollY.value,
                     hidden: checkHidden(ex, ey) }];
    }

    sendMessageToJava({
      type: "TextSelection:PositionHandles",
      positions: positions,
      rtl: this._isRTL
    });
  },

  subdocumentScrolled: function sh_subdocumentScrolled(aElement) {
    if (this._activeType == this.TYPE_NONE) {
      return;
    }
    let scrollView = aElement.ownerDocument.defaultView;
    let view = this._contentWindow;
    while (true) {
      if (view == scrollView) {
        // The selection is in a view (or sub-view) of the view that scrolled.
        // So we need to reposition the handles.
        if (this._activeType == this.TYPE_SELECTION) {
          this._updateCacheForSelection();
        }
        this._positionHandles();
        break;
      }
      if (view == view.parent) {
        break;
      }
      view = view.parent;
    }
  }
};
