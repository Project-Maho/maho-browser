(function() {
    let currentSelection = null;

    document.addEventListener('selectionchange', function() {
        const sel = window.getSelection();
        if (!sel || sel.isCollapsed || sel.rangeCount === 0) {
            window.webkit.messageHandlers.inlineEdit.postMessage({ type: 'cancel' });
            currentSelection = null;
            return;
        }

        const range = sel.getRangeAt(0);
        const container = range.commonAncestorContainer;
        const element = container.nodeType === 1 ? container : container.parentElement;
        const editableParent = element
            ? element.closest('[contenteditable="true"], input, textarea')
            : null;

        if (!editableParent) return;

        const text = sel.toString();
        if (text.trim().length < 2) return;

        const rect = range.getBoundingClientRect();
        currentSelection = { range: range.cloneRange(), element: editableParent };

        window.webkit.messageHandlers.inlineEdit.postMessage({
            type: 'selection',
            text: text,
            rect: JSON.stringify({ x: rect.x, y: rect.y, width: rect.width, height: rect.height }),
            isInput: editableParent.tagName === 'INPUT' || editableParent.tagName === 'TEXTAREA'
        });
    });

    window.__mahoReplaceSelection = function(newText) {
        if (!currentSelection) return false;
        const { range, element } = currentSelection;

        if (element.tagName === 'INPUT' || element.tagName === 'TEXTAREA') {
            const start = element.selectionStart;
            const end = element.selectionEnd;
            element.value = element.value.substring(0, start) + newText + element.value.substring(end);
            element.selectionStart = start;
            element.selectionEnd = start + newText.length;
            element.dispatchEvent(new Event('input', { bubbles: true }));
        } else {
            const sel = window.getSelection();
            sel.removeAllRanges();
            sel.addRange(range);
            document.execCommand('insertText', false, newText);
        }
        currentSelection = null;
        return true;
    };
})();
