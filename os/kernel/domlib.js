/*
 * Was Skripte von einer Seite sehen: window, document, Elemente, Ereignisse.
 * Dieser Teil ist selbst in JavaScript geschrieben und läuft in jeder Seite
 * vor deren Skripten. Er stützt sich auf wenige eingebaute Grundfunktionen
 * (dom.*, aus dom_js.c), die alles prüfen, was sie bekommen.
 */
(function (window) {
    var dom = window.__dom;
    delete window.__dom;

    function hide(o, name, value) {
        Object.defineProperty(o, name, { value: value, writable: true, configurable: true });
    }
    function accessor(o, name, get, set) {
        Object.defineProperty(o, name, { get: get, set: set, enumerable: true, configurable: true });
    }
    function text(v) { return v === null || v === undefined ? '' : String(v); }

    /* --- Ereignisse ------------------------------------------------------------------- */

    function EventTarget() {}
    var ET = EventTarget.prototype;
    ET.addEventListener = function (type, fn, options) {
        if (typeof fn !== 'function' && !(fn && typeof fn.handleEvent === 'function')) return;
        if (!this.__l) hide(this, '__l', {});
        var list = this.__l[type] || (this.__l[type] = []);
        if (type === 'mousemove' || type === 'pointermove') dom.wants(1);     /* der Browser meldet Bewegungen nur, wenn jemand zuhört */
        for (var i = 0; i < list.length; i++) if (list[i].fn === fn) return;
        list.push({ fn: fn, once: !!(options && typeof options === 'object' && options.once) });
    };
    ET.removeEventListener = function (type, fn) {
        var list = this.__l && this.__l[type];
        for (var i = 0; list && i < list.length; i++) {
            if (list[i].fn === fn) { list.splice(i, 1); return; }
        }
    };
    ET.dispatchEvent = function (event) { return dispatch(this, event); };

    function Event(type, init) {
        this.type = String(type);
        this.bubbles = !!(init && init.bubbles);
        this.cancelable = !!(init && init.cancelable);
        this.defaultPrevented = false;
        this.target = this.currentTarget = this.srcElement = null;
        this.timeStamp = Date.now();
        this.isTrusted = false;
        hide(this, '_stop', false);
        hide(this, '_now', false);
        for (var k in init) if (!(k in this)) this[k] = init[k];
    }
    Event.prototype.preventDefault = function () {
        if (this.cancelable) { this.defaultPrevented = true; this.returnValue = false; }
    };
    Event.prototype.stopPropagation = function () { this._stop = true; };
    Event.prototype.stopImmediatePropagation = function () { this._stop = this._now = true; };
    Event.prototype.initEvent = function (type, bubbles, cancelable) {
        this.type = String(type); this.bubbles = !!bubbles; this.cancelable = !!cancelable;
    };
    Event.prototype.composedPath = function () { return []; };
    function eventClass(name, defaults) {
        var C = function (type, init) {
            Event.call(this, type, init);
            for (var k in defaults) if (!(k in this)) this[k] = defaults[k];
        };
        C.prototype = Object.create(Event.prototype);
        C.prototype.constructor = C;
        window[name] = C;
        return C;
    }
    var CustomEvent = eventClass('CustomEvent', { detail: null });
    CustomEvent.prototype.initCustomEvent = function (type, bubbles, cancelable, detail) {
        this.initEvent(type, bubbles, cancelable); this.detail = detail;
    };
    var MouseEvent = eventClass('MouseEvent', { clientX: 0, clientY: 0, pageX: 0, pageY: 0, button: 0,
                                                buttons: 0, ctrlKey: false, shiftKey: false,
                                                altKey: false, metaKey: false, detail: 1 });
    eventClass('UIEvent', { detail: 0 });
    eventClass('KeyboardEvent', { key: '', code: '', keyCode: 0, which: 0, ctrlKey: false,
                                  shiftKey: false, altKey: false, metaKey: false });
    eventClass('InputEvent', { data: null, inputType: '' });
    eventClass('FocusEvent', { relatedTarget: null });
    eventClass('PointerEvent', { clientX: 0, clientY: 0, button: 0, pointerId: 1 });
    eventClass('ErrorEvent', { message: '' });

    function report(e) {
        var message = e && e.name ? e.name + ': ' + e.message : String(e);
        if (typeof window.onerror === 'function') {
            try { if (window.onerror(message, '', e && e.line || 0, 0, e) === true) return; } catch (x) {}
        }
        console.error(message);
    }

    /* Der Text eines Attributs wie onclick="..." als Funktion */
    function attributeHandler(node, type) {
        var source = node === window ? document.body : node;
        if (!source || typeof source.getAttribute !== 'function') return null;
        var code = dom.attr(source, 'on' + type);
        if (code === null) return null;
        var cache = source.__h || (hide(source, '__h', {}), source.__h);
        if (!cache[type] || cache[type].code !== code) {
            var fn = null;
            try { fn = new Function('event', code); } catch (e) { report(e); }
            cache[type] = { code: code, fn: fn };
        }
        return cache[type].fn;
    }

    function runHandlers(node, event) {
        var own = Object.getOwnPropertyDescriptor(node, 'on' + event.type);
        var handler = own ? own.value : undefined;
        if (typeof handler !== 'function' && handler !== null) handler = attributeHandler(node, event.type);
        if (typeof handler === 'function') {
            try { if (handler.call(node, event) === false) event.preventDefault(); } catch (e) { report(e); }
        }
        var list = node.__l && node.__l[event.type];
        if (!list) return;
        list = list.slice();
        for (var i = 0; i < list.length && !event._now; i++) {
            var entry = list[i];
            if (entry.once) node.removeEventListener(event.type, entry.fn);
            try {
                if (typeof entry.fn === 'function') entry.fn.call(node, event);
                else entry.fn.handleEvent(event);
            } catch (e) { report(e); }
        }
    }

    function dispatch(target, event) {
        if (!event || typeof event.type !== 'string') throw new TypeError('dispatchEvent needs an Event');
        event.target = event.srcElement = target;
        var path = [], node = target;
        for (var guard = 0; node && guard < 5000; guard++) {
            path.push(node);
            if (node === window) break;
            node = node === document ? window : node instanceof Node ? dom.parent(node) : null;
        }
        for (var i = 0; i < path.length; i++) {
            if (i > 0 && !event.bubbles) break;
            event.currentTarget = path[i];
            runHandlers(path[i], event);
            if (event._stop) break;
        }
        event.currentTarget = null;
        return !event.defaultPrevented;
    }
    function fire(target, type, bubbles, cancelable) {
        return dispatch(target, new Event(type, { bubbles: bubbles !== false, cancelable: !!cancelable }));
    }

    /* --- Knoten ------------------------------------------------------------------------- */

    function Node() {}
    Node.prototype = Object.create(ET);
    var N = Node.prototype;
    N.ELEMENT_NODE = Node.ELEMENT_NODE = 1; N.TEXT_NODE = Node.TEXT_NODE = 3;
    N.COMMENT_NODE = Node.COMMENT_NODE = 8; N.DOCUMENT_NODE = Node.DOCUMENT_NODE = 9;
    N.DOCUMENT_FRAGMENT_NODE = Node.DOCUMENT_FRAGMENT_NODE = 11;
    accessor(N, 'nodeType', function () { return dom.type(this); });
    accessor(N, 'nodeName', function () {
        var t = dom.type(this);
        return t === 1 ? dom.name(this).toUpperCase() : t === 3 ? '#text' : t === 8 ? '#comment'
             : t === 9 ? '#document' : '#document-fragment';
    });
    accessor(N, 'nodeValue', function () { var t = dom.type(this); return t === 3 || t === 8 ? dom.text(this) : null; },
             function (v) { var t = dom.type(this); if (t === 3 || t === 8) dom.setText(this, text(v)); });
    accessor(N, 'data', function () { return dom.text(this); }, function (v) { dom.setText(this, text(v)); });
    accessor(N, 'parentNode', function () { return dom.parent(this); });
    accessor(N, 'parentElement', function () { var p = dom.parent(this); return p && dom.type(p) === 1 ? p : null; });
    accessor(N, 'firstChild', function () { return dom.first(this); });
    accessor(N, 'lastChild', function () { return dom.last(this); });
    accessor(N, 'nextSibling', function () { return dom.next(this); });
    accessor(N, 'previousSibling', function () { return dom.prev(this); });
    accessor(N, 'childNodes', function () { return dom.kids(this, false); });
    accessor(N, 'textContent', function () { return dom.type(this) === 9 ? null : dom.text(this); },
             function (v) { dom.setText(this, text(v)); });
    accessor(N, 'ownerDocument', function () { return this === document ? null : document; });
    accessor(N, 'isConnected', function () { return dom.contains(document, this); });
    N.appendChild = function (child) { dom.insert(this, child, null); return child; };
    N.insertBefore = function (child, ref) { dom.insert(this, child, ref || null); return child; };
    N.removeChild = function (child) {
        if (!child || dom.parent(child) !== this) throw new Error('NotFoundError: The node to be removed is not a child of this node');
        dom.remove(child);
        return child;
    };
    N.replaceChild = function (fresh, old) {
        if (!old || dom.parent(old) !== this) throw new Error('NotFoundError: The node to be replaced is not a child of this node');
        dom.insert(this, fresh, old);
        dom.remove(old);
        return old;
    };
    N.cloneNode = function (deep) { return dom.clone(this, !!deep); };
    N.contains = function (other) { return !!other && dom.contains(this, other); };
    N.hasChildNodes = function () { return dom.first(this) !== null; };
    N.normalize = function () {};
    N.getRootNode = function () { var n = this; for (var p; (p = dom.parent(n)); ) n = p; return n; };
    N.isSameNode = function (other) { return this === other; };
    N.isEqualNode = function (other) { return this === other || (!!other && dom.html(this, false) === dom.html(other, false)); };
    N.compareDocumentPosition = function (other) {
        if (this === other) return 0;
        if (dom.contains(this, other)) return 20;
        if (dom.contains(other, this)) return 10;
        return 4;
    };

    /* --- Elemente ----------------------------------------------------------------------- */

    function Element() {}
    Element.prototype = Object.create(N);
    function HTMLElement() {}
    HTMLElement.prototype = Object.create(Element.prototype);
    var E = Element.prototype, H = HTMLElement.prototype;

    function many(nodes) {             /* Liste von Knoten: ein Feld, das auch item() kennt */
        hide(nodes, 'item', function (i) { return this[i] || null; });
        hide(nodes, 'namedItem', function (name) {
            for (var i = 0; i < this.length; i++) {
                if (dom.attr(this[i], 'id') === name || dom.attr(this[i], 'name') === name) return this[i];
            }
            return null;
        });
        return nodes;
    }

    accessor(E, 'tagName', function () { return dom.name(this).toUpperCase(); });
    accessor(E, 'localName', function () { return dom.name(this); });
    accessor(E, 'id', function () { return text(dom.attr(this, 'id')); }, function (v) { dom.setAttr(this, 'id', text(v)); });
    accessor(E, 'className', function () { return text(dom.attr(this, 'class')); }, function (v) { dom.setAttr(this, 'class', text(v)); });
    accessor(E, 'innerHTML', function () { return dom.html(this, true); }, function (v) { dom.setHtml(this, text(v)); });
    accessor(E, 'outerHTML', function () { return dom.html(this, false); }, function (v) {
        var parent = dom.parent(this);
        if (!parent) return;
        dom.insert(parent, dom.parse(text(v)), this);
        dom.remove(this);
    });
    accessor(E, 'innerText', function () { return dom.text(this); }, function (v) { dom.setText(this, text(v)); });
    accessor(E, 'children', function () { return many(dom.kids(this, true)); });
    accessor(E, 'childElementCount', function () { return dom.kids(this, true).length; });
    accessor(E, 'firstElementChild', function () { return dom.kids(this, true)[0] || null; });
    accessor(E, 'lastElementChild', function () { var k = dom.kids(this, true); return k[k.length - 1] || null; });
    accessor(E, 'nextElementSibling', function () {
        for (var n = dom.next(this); n; n = dom.next(n)) if (dom.type(n) === 1) return n;
        return null;
    });
    accessor(E, 'previousElementSibling', function () {
        for (var n = dom.prev(this); n; n = dom.prev(n)) if (dom.type(n) === 1) return n;
        return null;
    });
    accessor(E, 'attributes', function () {
        return many(dom.attrs(this).map(function (a) { return { name: a[0], value: a[1], nodeName: a[0], nodeValue: a[1] }; }));
    });
    accessor(E, 'style', function () { return this.__s || (hide(this, '__s', dom.style(this)), this.__s); },
             function (v) { dom.setAttr(this, 'style', text(v)); });
    accessor(E, 'dataset', function () { return this.__d || (hide(this, '__d', dom.dataset(this)), this.__d); });

    /*
     * Stylesheets als Objekte: Was ein Skript mit insertRule() einfügt, wird
     * dem Text des <style>-Elements angehängt - dort liest der Browser die
     * Gestaltung der Seite.
     */
    function CSSStyleSheet(owner) {
        hide(this, 'ownerNode', owner);
        hide(this, '_base', dom.text(owner));
        hide(this, '_rules', []);
        hide(this, 'disabled', false);
    }
    function syncSheet(sheet) {
        var out = sheet._base;
        for (var i = 0; i < sheet._rules.length; i++) out += '\n' + sheet._rules[i].cssText;
        dom.setText(sheet.ownerNode, out);
    }
    CSSStyleSheet.prototype.insertRule = function (rule, index) {
        index = index === undefined ? 0 : index >>> 0;
        if (index > this._rules.length) throw new Error('Failed to insert the rule: index out of range');
        this._rules.splice(index, 0, { cssText: text(rule), type: 1, style: {} });
        syncSheet(this);
        return index;
    };
    CSSStyleSheet.prototype.deleteRule = function (index) {
        if (index >>> 0 >= this._rules.length) throw new Error('Failed to delete the rule: index out of range');
        this._rules.splice(index >>> 0, 1);
        syncSheet(this);
    };
    CSSStyleSheet.prototype.addRule = function (selector, style, index) {
        return this.insertRule(selector + ' {' + style + '}', index === undefined ? this._rules.length : index);
    };
    CSSStyleSheet.prototype.removeRule = CSSStyleSheet.prototype.deleteRule;
    accessor(CSSStyleSheet.prototype, 'cssRules', function () { return this._rules; });
    accessor(CSSStyleSheet.prototype, 'rules', function () { return this._rules; });
    accessor(CSSStyleSheet.prototype, 'href', function () { return this.ownerNode.href || null; });
    accessor(E, 'sheet', function () {
        var n = dom.name(this);
        if (n !== 'style' && !(n === 'link' && /stylesheet/i.test(text(dom.attr(this, 'rel'))))) return null;
        return this.__sheet || (hide(this, '__sheet', new CSSStyleSheet(this)), this.__sheet);
    });
    accessor(E, 'classList', function () { return this.__c || (hide(this, '__c', new ClassList(this)), this.__c); },
             function (v) { dom.setAttr(this, 'class', text(v)); });

    E.getAttribute = function (name) { return dom.attr(this, String(name)); };
    E.setAttribute = function (name, value) { dom.setAttr(this, String(name), text(value)); };
    E.removeAttribute = function (name) { dom.delAttr(this, String(name)); };
    E.hasAttribute = function (name) { return dom.attr(this, String(name)) !== null; };
    E.hasAttributes = function () { return dom.attrs(this).length > 0; };
    E.toggleAttribute = function (name, force) {
        var on = force === undefined ? dom.attr(this, name) === null : !!force;
        if (on) dom.setAttr(this, name, ''); else dom.delAttr(this, name);
        return on;
    };
    E.getAttributeNames = function () { return dom.attrs(this).map(function (a) { return a[0]; }); };
    E.getAttributeNode = function (name) { var v = dom.attr(this, name); return v === null ? null : { name: name, value: v }; };
    E.querySelector = N.querySelector = function (selector) { return dom.query(this, String(selector), false); };
    E.querySelectorAll = N.querySelectorAll = function (selector) { return many(dom.query(this, String(selector), true)); };
    E.getElementsByTagName = N.getElementsByTagName = function (name) { return many(dom.byTag(this, String(name))); };
    E.getElementsByClassName = N.getElementsByClassName = function (names) { return many(dom.byClass(this, String(names))); };
    E.matches = E.webkitMatchesSelector = E.msMatchesSelector = function (selector) { return dom.matches(this, String(selector)); };
    E.closest = function (selector) {
        for (var n = this; n && dom.type(n) === 1; n = dom.parent(n)) if (dom.matches(n, String(selector))) return n;
        return null;
    };
    function toNode(item) { return typeof item === 'object' && item !== null ? item : dom.textNode(text(item)); }
    function gather(items) {
        if (items.length === 1) return toNode(items[0]);
        var f = dom.frag();
        for (var i = 0; i < items.length; i++) dom.insert(f, toNode(items[i]), null);
        return f;
    }
    E.remove = function () { dom.remove(this); };
    E.append = N.append = function () { dom.insert(this, gather(arguments), null); };
    E.prepend = N.prepend = function () { dom.insert(this, gather(arguments), dom.first(this)); };
    E.before = function () { var p = dom.parent(this); if (p) dom.insert(p, gather(arguments), this); };
    E.after = function () { var p = dom.parent(this); if (p) dom.insert(p, gather(arguments), dom.next(this)); };
    E.replaceWith = function () { var p = dom.parent(this); if (p) { dom.insert(p, gather(arguments), this); dom.remove(this); } };
    E.replaceChildren = N.replaceChildren = function () { dom.setText(this, ''); if (arguments.length) dom.insert(this, gather(arguments), null); };
    function adjacent(el, where, node) {
        var parent = dom.parent(el);
        switch (String(where).toLowerCase()) {
        case 'beforebegin': if (parent) dom.insert(parent, node, el); break;
        case 'afterbegin': dom.insert(el, node, dom.first(el)); break;
        case 'beforeend': dom.insert(el, node, null); break;
        case 'afterend': if (parent) dom.insert(parent, node, dom.next(el)); break;
        default: throw new SyntaxError('Invalid position');
        }
    }
    E.insertAdjacentHTML = function (where, html) { adjacent(this, where, dom.parse(text(html))); };
    E.insertAdjacentElement = function (where, el) { adjacent(this, where, el); return el; };
    E.insertAdjacentText = function (where, t) { adjacent(this, where, dom.textNode(text(t))); };
    /*
     * Maße: Wo ein Element steht und wie groß es ist, ergibt sich erst, wenn die Seite
     * gesetzt wird - dom.rect() fragt den Seitenaufbau (und lässt die Seite dafür setzen,
     * wenn sie sich seit dem letzten Mal geändert hat). Wo niemand die Seite setzen kann,
     * gilt eine Annahme: Sichtbares hat eine feste Größe am Seitenanfang.
     */
    function wholePage(el) { var n = dom.name(el); return n === 'html' || n === 'body'; }
    function DOMRect(x, y, width, height) {
        this.x = this.left = +x || 0; this.y = this.top = +y || 0;
        this.width = +width || 0; this.height = +height || 0;
        this.right = this.x + this.width; this.bottom = this.y + this.height;
    }
    DOMRect.prototype.toJSON = function () {
        return { x: this.x, y: this.y, width: this.width, height: this.height,
                 top: this.top, right: this.right, bottom: this.bottom, left: this.left };
    };
    window.DOMRect = window.DOMRectReadOnly = DOMRect;
    /*
     * Lage auf der ganzen Seite: [x, y, Breite, Höhe, Rahmen oben, rechts, unten, links,
     * im Text, Innenabstand oben, rechts, unten, links] - oder null: nicht zu sehen.
     */
    function place(el) {
        var r = dom.rect(el);
        if (r !== undefined) return r;
        if (!dom.shown(el)) return null;
        return wholePage(el) ? [0, 0, window.innerWidth, window.innerHeight, 0, 0, 0, 0, false, 0, 0, 0, 0]
                             : [0, 0, 100, 20, 0, 0, 0, 0, false, 0, 0, 0, 0];
    }
    /* ... und im sichtbaren Ausschnitt */
    function box(el) {
        var r = place(el);
        return r ? new DOMRect(r[0], r[1] - dom.scroll(), r[2], r[3]) : new DOMRect(0, 0, 0, 0);
    }
    E.getBoundingClientRect = function () { return box(this); };
    E.getClientRects = function () { return place(this) ? [box(this)] : []; };
    E.scrollIntoView = function (how) {
        var r = place(this);
        if (!r) return;
        var block = how === false ? 'end' : how && typeof how === 'object' && how.block ? String(how.block) : 'start';
        var top = dom.scroll(), view = window.innerHeight;
        if (block === 'nearest') {
            if (r[1] >= top && r[1] + r[3] <= top + view) return;
            block = r[1] < top ? 'start' : 'end';
        }
        window.scrollTo(0, block === 'center' ? r[1] - (view - r[3]) / 2 : block === 'end' ? r[1] + r[3] - view : r[1]);
    };
    E.scrollTo = E.scroll = function (x, y) { if (dom.name(this) === 'html') window.scrollTo(x, y); };
    E.scrollBy = function (x, y) { if (dom.name(this) === 'html') window.scrollBy(x, y); };
    E.focus = E.blur = E.select = E.setSelectionRange = function () {};
    E.attachEvent = function (name, fn) { this.addEventListener(String(name).replace(/^on/, ''), fn); };
    E.detachEvent = function (name, fn) { this.removeEventListener(String(name).replace(/^on/, ''), fn); };
    /* offsetTop und offsetLeft zählen ab dem Element, nach dem sich die Lage richtet (offsetParent) */
    function offset(el, i) {
        var r = place(el);
        if (!r) return 0;
        var anchor = dom.anchor(el), p = anchor && place(anchor);
        return p ? r[i] - p[i] - (i ? p[4] : p[7]) : r[i];
    }
    accessor(E, 'offsetParent', function () { return place(this) ? dom.anchor(this) : null; });
    accessor(E, 'offsetLeft', function () { return offset(this, 0); }, function () {});
    accessor(E, 'offsetTop', function () { return offset(this, 1); }, function () {});
    accessor(E, 'offsetWidth', function () { var r = place(this); return r ? r[2] : 0; }, function () {});
    accessor(E, 'offsetHeight', function () { var r = place(this); return r ? r[3] : 0; }, function () {});
    accessor(E, 'clientLeft', function () { var r = place(this); return r ? r[7] : 0; }, function () {});
    accessor(E, 'clientTop', function () { var r = place(this); return r ? r[4] : 0; }, function () {});
    /* die Fläche innerhalb des Rahmens; für <html> der sichtbare Ausschnitt */
    function inner(el, i) {
        if (dom.name(el) === 'html') return i ? window.innerHeight : window.innerWidth;
        var r = place(el);
        if (!r || r[8]) return 0;
        return Math.max(0, i ? r[3] - r[4] - r[6] : r[2] - r[5] - r[7]);
    }
    accessor(E, 'clientWidth', function () { return inner(this, 0); }, function () {});
    accessor(E, 'clientHeight', function () { return inner(this, 1); }, function () {});
    /* Rollen lässt sich hier nur die ganze Seite: Bei allem anderen ist der Inhalt so groß wie der Kasten. */
    function whole(el, i) {
        var page = dom.name(el) === 'html' ? dom.page() : undefined;
        return page ? Math.max(page[i], inner(el, i)) : inner(el, i);
    }
    accessor(E, 'scrollWidth', function () { return whole(this, 0); }, function () {});
    accessor(E, 'scrollHeight', function () { return whole(this, 1); }, function () {});
    accessor(E, 'scrollLeft', function () { return 0; }, function () {});
    accessor(E, 'scrollTop', function () { return dom.name(this) === 'html' ? dom.scroll() : 0; },
             function (y) { if (dom.name(this) === 'html') window.scrollTo(0, y); });

    /* Eigenschaften, die ein Attribut spiegeln */
    [['title'], ['lang'], ['dir'], ['name'], ['rel'], ['target'], ['method'], ['placeholder'],
     ['alt'], ['htmlFor', 'for'], ['accept'], ['media'], ['charset'], ['content'], ['role'], ['enctype'],
     ['httpEquiv', 'http-equiv'], ['label'], ['pattern'], ['min'], ['max'], ['step'], ['autocomplete'],
     ['align'], ['accessKey', 'accesskey'], ['nonce'], ['crossOrigin', 'crossorigin'], ['srcset'], ['sizes'],
     ['width'], ['height'], ['rows'], ['cols'], ['colSpan', 'colspan'], ['rowSpan', 'rowspan'],
     ['maxLength', 'maxlength'], ['tabIndex', 'tabindex'], ['defaultValue', 'value']].forEach(function (p) {
        var attr = p[1] || p[0];
        accessor(H, p[0], function () { return text(dom.attr(this, attr)); }, function (v) { dom.setAttr(this, attr, text(v)); });
    });
    [['href'], ['src'], ['action'], ['poster'], ['cite'], ['formAction', 'formaction']].forEach(function (p) {
        var attr = p[1] || p[0];
        accessor(H, p[0], function () { var v = dom.attr(this, attr); return v === null ? '' : dom.resolve(v); },
                 function (v) { dom.setAttr(this, attr, text(v)); });
    });
    [['disabled'], ['hidden'], ['readOnly', 'readonly'], ['required'], ['multiple'], ['autofocus'], ['async'],
     ['defer'], ['open'], ['noValidate', 'novalidate'], ['defaultChecked', 'checked'], ['controls'],
     ['loop'], ['muted'], ['autoplay'], ['noModule', 'nomodule']].forEach(function (p) {
        var attr = p[1] || p[0];
        accessor(H, p[0], function () { return dom.attr(this, attr) !== null; },
                 function (v) { if (v) dom.setAttr(this, attr, ''); else dom.delAttr(this, attr); });
    });

    /* Formulare */
    function inputType(el) { return (dom.attr(el, 'type') || '').toLowerCase(); }
    /* "type": bei Formularfeldern die Art des Felds, auch wenn das Attribut fehlt */
    accessor(H, 'type', function () {
        var n = dom.name(this), t = dom.attr(this, 'type');
        if (n === 'input') return t === null || t === '' ? 'text' : t.toLowerCase();
        if (n === 'button') return t === null || !/^(button|reset)$/i.test(t) ? 'submit' : t.toLowerCase();
        if (n === 'select') return dom.attr(this, 'multiple') !== null ? 'select-multiple' : 'select-one';
        if (n === 'textarea') return 'textarea';
        return text(t);
    }, function (v) { dom.setAttr(this, 'type', text(v)); });
    accessor(H, 'value', function () { return dom.value(this); }, function (v) { dom.setValue(this, text(v)); });
    accessor(H, 'checked', function () { return dom.checked(this); }, function (v) {
        v = !!v;
        if (v && inputType(this) === 'radio') {
            var name = dom.attr(this, 'name'), scope = this.form || document, all = dom.byTag(scope, 'input');
            for (var i = 0; name && i < all.length; i++) {
                if (all[i] !== this && inputType(all[i]) === 'radio' && dom.attr(all[i], 'name') === name) dom.setChecked(all[i], false);
            }
        }
        dom.setChecked(this, v);
    });
    accessor(H, 'selectedIndex', function () { return dom.selected(this); }, function (v) { dom.setSelected(this, v | 0); });
    accessor(H, 'options', function () {
        var n = dom.name(this);
        return n === 'select' || n === 'datalist' ? many(dom.byTag(this, 'option')) : undefined;
    });
    accessor(H, 'selectedOptions', function () { var o = dom.byTag(this, 'option'), i = dom.selected(this); return many(o[i] ? [o[i]] : []); });
    accessor(H, 'selected', function () {
        var list = this.closest('select');
        return list ? dom.byTag(list, 'option')[dom.selected(list)] === this : dom.attr(this, 'selected') !== null;
    }, function (v) {
        var list = this.closest('select');
        if (list && v) dom.setSelected(list, dom.byTag(list, 'option').indexOf(this));
        else if (v) dom.setAttr(this, 'selected', ''); else dom.delAttr(this, 'selected');
    });
    accessor(H, 'index', function () { var list = this.closest('select'); return list ? dom.byTag(list, 'option').indexOf(this) : 0; });
    accessor(H, 'text', function () { return dom.text(this); }, function (v) { dom.setText(this, text(v)); });
    accessor(H, 'form', function () { return this.closest('form'); });
    accessor(H, 'elements', function () {
        if (dom.name(this) !== 'form' && dom.name(this) !== 'fieldset') return undefined;
        var out = [], all = dom.byTag(this, '*');
        for (var i = 0; i < all.length; i++) {
            var n = dom.name(all[i]);
            if (n === 'input' || n === 'select' || n === 'textarea' || n === 'button') {
                out.push(all[i]);
                var key = dom.attr(all[i], 'name') || dom.attr(all[i], 'id');
                if (key && !(key in out)) hide(out, key, all[i]);
            }
        }
        return many(out);
    });
    accessor(H, 'length', function () {
        var n = dom.name(this);
        return n === 'form' ? this.elements.length : n === 'select' ? dom.byTag(this, 'option').length : undefined;
    });
    accessor(H, 'labels', function () { return many([]); });
    accessor(H, 'validity', function () { return { valid: true }; });
    H.checkValidity = H.reportValidity = function () { return true; };
    H.setCustomValidity = function () {};

    function submitForm(form, button) {
        if (!form) return;
        var event = new Event('submit', { bubbles: true, cancelable: true });
        event.submitter = button || null;
        if (dispatch(form, event)) dom.wish(2, form, button || null);
    }
    function checkedInGroup(radio) {
        var name = dom.attr(radio, 'name'), all = dom.byTag(radio.form || document, 'input');
        for (var i = 0; name && i < all.length; i++) {
            if (all[i] !== radio && inputType(all[i]) === 'radio' && dom.attr(all[i], 'name') === name &&
                dom.checked(all[i])) return all[i];
        }
        return null;
    }
    /*
     * Klick auf ein Ankreuz- oder Auswahlfeld: erst umschalten, dann die Seite fragen -
     * so sehen es die Skripte auch in anderen Browsern. Lehnt sie ab, gilt wieder der alte Stand.
     */
    function clickCheckable(node, click) {
        var radio = inputType(node) === 'radio';
        var was = dom.checked(node), other = radio && !was ? checkedInGroup(node) : null;
        node.checked = radio ? true : !was;
        if (!dispatch(node, click)) {
            node.checked = was;
            if (other) other.checked = true;
        } else if (dom.checked(node) !== was) {
            fire(node, 'input'); fire(node, 'change');
        }
    }
    /* Was ein Klick bewirkt, den ein Skript auslöst (echte Klicks behandelt der Browser) */
    function defaultAction(el) {
        var name = dom.name(el), type = inputType(el);
        if (name === 'a' && dom.attr(el, 'href') !== null) {
            dom.wish(1, dom.attr(el, 'href'));
        } else if ((name === 'input' && (type === 'submit' || type === 'image')) ||
                   (name === 'button' && (type === 'submit' || type === ''))) {
            submitForm(el.form, el);
        }
    }
    H.click = function () {
        if (dom.attr(this, 'disabled') !== null) return;
        var click = new MouseEvent('click', { bubbles: true, cancelable: true });
        if (dom.name(this) === 'input' && (inputType(this) === 'checkbox' || inputType(this) === 'radio')) clickCheckable(this, click);
        else if (dispatch(this, click)) defaultAction(this);
    };
    H.submit = function () { if (dom.name(this) === 'form') dom.wish(2, this, null); };
    /* <dialog>: zeigen und schließen */
    H.show = H.showModal = function () { if (dom.name(this) === 'dialog') dom.setAttr(this, 'open', ''); };
    H.close = function (value) {
        if (dom.name(this) !== 'dialog' || dom.attr(this, 'open') === null) return;
        dom.delAttr(this, 'open');
        if (value !== undefined) this.returnValue = String(value);
        fire(this, 'close', false);
    };
    accessor(H, 'open', function () { return dom.attr(this, 'open') !== null; },
             function (v) { if (v) dom.setAttr(this, 'open', ''); else dom.delAttr(this, 'open'); });
    H.requestSubmit = function (button) { if (dom.name(this) === 'form') submitForm(this, button); };
    H.reset = function () {
        var fields = this.elements;
        for (var i = 0; i < fields.length; i++) {
            var f = fields[i], n = dom.name(f);
            if (n === 'input' && (inputType(f) === 'checkbox' || inputType(f) === 'radio')) dom.setChecked(f, dom.attr(f, 'checked') !== null);
            else if (n === 'input') dom.setValue(f, text(dom.attr(f, 'value')));
            else if (n === 'textarea') dom.setValue(f, dom.text(f));
            else if (n === 'select') dom.setSelected(f, -2);
        }
        fire(this, 'reset');
    };

    function ClassList(el) { hide(this, '_el', el); }
    var CL = ClassList.prototype;
    function classes(list) { return text(dom.attr(list._el, 'class')).split(/\s+/).filter(Boolean); }
    function setClasses(list, names) { dom.setAttr(list._el, 'class', names.join(' ')); }
    CL.contains = function (name) { return classes(this).indexOf(String(name)) >= 0; };
    CL.add = function () {
        var names = classes(this);
        for (var i = 0; i < arguments.length; i++) if (names.indexOf(String(arguments[i])) < 0) names.push(String(arguments[i]));
        setClasses(this, names);
    };
    CL.remove = function () {
        var gone = Array.prototype.map.call(arguments, String);
        setClasses(this, classes(this).filter(function (n) { return gone.indexOf(n) < 0; }));
    };
    CL.toggle = function (name, force) {
        var on = force === undefined ? !this.contains(name) : !!force;
        if (on) this.add(name); else this.remove(name);
        return on;
    };
    CL.replace = function (from, to) {
        var names = classes(this), i = names.indexOf(String(from));
        if (i < 0) return false;
        names[i] = String(to);
        setClasses(this, names);
        return true;
    };
    CL.item = function (i) { return classes(this)[i] || null; };
    CL.forEach = function (fn, self) { classes(this).forEach(fn, self); };
    CL.values = function () { return classes(this)[Symbol.iterator](); };
    hide(CL, Symbol.iterator, CL.values);
    CL.toString = function () { return text(dom.attr(this._el, 'class')); };
    accessor(CL, 'length', function () { return classes(this).length; });
    accessor(CL, 'value', function () { return this.toString(); }, function (v) { dom.setAttr(this._el, 'class', text(v)); });

    /* --- Arten von Knoten, damit instanceof stimmt ----------------------------------------- */

    function kind(name, base) {
        var C = function () { throw new TypeError('Illegal constructor'); };
        C.prototype = Object.create(base);
        C.prototype.constructor = C;
        window[name] = C;
        return C.prototype;
    }
    var textProto = kind('Text', N), commentProto = kind('Comment', N);
    var fragmentProto = kind('DocumentFragment', N), documentProto = kind('Document', N);
    window.CharacterData = window.Text;
    window.HTMLDocument = window.Document;
    textProto.splitText = function () { return this; };
    accessor(textProto, 'length', function () { return dom.text(this).length; });
    accessor(textProto, 'wholeText', function () { return dom.text(this); });
    fragmentProto.getElementById = function (id) { return dom.byId(this, String(id)); };
    accessor(fragmentProto, 'children', function () { return many(dom.kids(this, true)); });
    accessor(fragmentProto, 'firstElementChild', function () { return dom.kids(this, true)[0] || null; });

    var tags = {};
    [['HTMLAnchorElement', 'a'], ['HTMLBodyElement', 'body'], ['HTMLButtonElement', 'button'],
     ['HTMLCanvasElement', 'canvas'], ['HTMLDivElement', 'div'], ['HTMLFormElement', 'form'],
     ['HTMLHeadingElement', 'h1 h2 h3 h4 h5 h6'], ['HTMLHeadElement', 'head'], ['HTMLHtmlElement', 'html'],
     ['HTMLIFrameElement', 'iframe'], ['HTMLImageElement', 'img'], ['HTMLInputElement', 'input'],
     ['HTMLLabelElement', 'label'], ['HTMLLIElement', 'li'], ['HTMLLinkElement', 'link'],
     ['HTMLMetaElement', 'meta'], ['HTMLOptionElement', 'option'], ['HTMLParagraphElement', 'p'],
     ['HTMLScriptElement', 'script'], ['HTMLSelectElement', 'select'], ['HTMLSpanElement', 'span'],
     ['HTMLStyleElement', 'style'], ['HTMLTableElement', 'table'], ['HTMLTableCellElement', 'td th'],
     ['HTMLTableRowElement', 'tr'], ['HTMLTextAreaElement', 'textarea'], ['HTMLUListElement', 'ul'],
     ['HTMLOListElement', 'ol'], ['HTMLTitleElement', 'title'], ['HTMLVideoElement', 'video'],
     ['HTMLAudioElement', 'audio'], ['HTMLTemplateElement', 'template'], ['HTMLPreElement', 'pre'],
     ['HTMLFieldSetElement', 'fieldset'], ['HTMLUnknownElement', '']].forEach(function (entry) {
        var proto = kind(entry[0], H);
        entry[1].split(' ').forEach(function (tag) { if (tag) tags[tag] = proto; });
    });
    window.Node = Node; window.Element = Element; window.HTMLElement = HTMLElement;
    window.EventTarget = EventTarget; window.Event = Event;
    window.NodeList = window.HTMLCollection = Array;
    window.DOMTokenList = ClassList;
    dom.protos({ element: H, text: textProto, comment: commentProto, fragment: fragmentProto,
                 document: documentProto, tags: tags });

    /* --- document ------------------------------------------------------------------------- */

    var document = dom.doc(), D = documentProto, ready = 'loading', cookies = [];
    D.getElementById = function (id) { return dom.byId(this, String(id)); };
    /* das Element an einer Stelle des sichtbaren Ausschnitts (soweit die Lage bekannt ist) */
    D.elementFromPoint = function (x, y) {
        x = +x; y = +y;
        if (!(x >= 0 && y >= 0 && x < window.innerWidth && y < window.innerHeight)) return null;
        return dom.at(x, y + dom.scroll()) || this.documentElement;
    };
    D.elementsFromPoint = function (x, y) {
        var list = [];
        for (var el = this.elementFromPoint(x, y); el && dom.type(el) === 1; el = dom.parent(el)) list.push(el);
        return list;
    };
    D.getElementsByName = function (name) { return many(dom.query(this, '[name="' + String(name).replace(/["\\]/g, '') + '"]', true)); };
    D.createElement = function (name) { return dom.el(String(name)); };
    D.createElementNS = function (ns, name) { return dom.el(String(name).replace(/^.*:/, '')); };
    D.createTextNode = function (t) { return dom.textNode(text(t)); };
    D.createComment = function (t) { return dom.comment(text(t)); };
    D.createDocumentFragment = function () { return dom.frag(); };
    D.createEvent = function () { return new Event(''); };
    D.write = function () { dom.write(Array.prototype.join.call(arguments, '')); };
    D.writeln = function () { dom.write(Array.prototype.join.call(arguments, '') + '\n'); };
    D.open = D.close = function () {};
    D.hasFocus = function () { return true; };
    D.execCommand = function () { return false; };
    D.importNode = function (node, deep) { return dom.clone(node, !!deep); };
    D.adoptNode = function (node) { return node; };
    accessor(D, 'documentElement', function () { return dom.query(this, 'html', false); });
    accessor(D, 'head', function () { return dom.query(this, 'head', false); });
    accessor(D, 'body', function () { return dom.query(this, 'body', false); });
    accessor(D, 'title', function () { var t = dom.query(this, 'title', false); return t ? dom.text(t).trim() : ''; },
             function (v) {
                 var t = dom.query(this, 'title', false);
                 if (!t) { t = dom.el('title'); var head = this.head; if (head) dom.insert(head, t, null); }
                 dom.setText(t, text(v));
             });
    accessor(D, 'readyState', function () { return ready; });
    accessor(D, 'cookie', function () { return cookies.join('; '); }, function (v) {
        var pair = text(v).split(';')[0], name = pair.split('=')[0].trim();
        cookies = cookies.filter(function (c) { return c.split('=')[0] !== name; });
        if (!/max-age\s*=\s*0|expires\s*=[^;]*19[0-9][0-9]/i.test(text(v))) cookies.push(pair.trim());
    });
    accessor(D, 'forms', function () { return many(dom.byTag(this, 'form')); });
    accessor(D, 'images', function () { return many(dom.byTag(this, 'img')); });
    accessor(D, 'links', function () { return many(dom.query(this, 'a[href]', true)); });
    accessor(D, 'scripts', function () { return many(dom.byTag(this, 'script')); });
    accessor(D, 'styleSheets', function () {
        return many(dom.query(this, 'style, link[rel~=stylesheet]', true)).map(function (el) { return el.sheet; });
    });
    accessor(D, 'currentScript', function () { return dom.script(); });
    accessor(D, 'defaultView', function () { return window; });
    accessor(D, 'activeElement', function () { return dom.focused() || this.body; });
    accessor(D, 'URL', function () { return dom.url(); });
    accessor(D, 'documentURI', function () { return dom.url(); });
    accessor(D, 'baseURI', function () { return dom.url(); });
    accessor(D, 'domain', function () { return location.hostname; });
    accessor(D, 'children', function () { return many(dom.kids(this, true)); });
    D.referrer = ''; D.characterSet = D.charset = 'UTF-8'; D.compatMode = 'CSS1Compat';
    D.hidden = false; D.visibilityState = 'visible'; D.contentType = 'text/html';
    D.implementation = {
        hasFeature: function () { return false; },
        /* Ein zweites, loses Dokument (Bibliotheken zerlegen darin Quelltext): Kopf und Rumpf ohne Seite */
        createHTMLDocument: function (title) {
            var root = dom.el('html'), head = dom.el('head'), body = dom.el('body');
            dom.insert(root, head, null);
            dom.insert(root, body, null);
            if (title !== undefined) {
                var t = dom.el('title');
                dom.setText(t, text(title));
                dom.insert(head, t, null);
            }
            return {
                nodeType: 9, nodeName: '#document', documentElement: root, head: head, body: body,
                createElement: D.createElement, createElementNS: D.createElementNS,
                createTextNode: D.createTextNode, createComment: D.createComment,
                createDocumentFragment: D.createDocumentFragment, implementation: D.implementation,
                getElementById: function (id) { return dom.byId(root, String(id)); },
                getElementsByTagName: function (name) { return root.getElementsByTagName(name); },
                querySelector: function (q) { return root.querySelector(q); },
                querySelectorAll: function (q) { return root.querySelectorAll(q); },
                addEventListener: function () {}, removeEventListener: function () {},
                defaultView: null, readyState: 'complete', title: title === undefined ? '' : text(title)
            };
        }
    };

    /* --- window --------------------------------------------------------------------------- */

    var location = {};
    function part(i) {
        var m = /^([a-z][a-z0-9+.\-]*:)\/\/([^\/?#:]*)(?::(\d+))?([^?#]*)(\?[^#]*)?(#.*)?$/i.exec(dom.url());
        return m && m[i] ? m[i] : '';
    }
    accessor(location, 'href', function () { return dom.url(); }, function (v) { dom.wish(1, text(v)); });
    accessor(location, 'protocol', function () { return part(1); });
    accessor(location, 'hostname', function () { return part(2); });
    accessor(location, 'port', function () { return part(3); });
    accessor(location, 'host', function () { return part(2) + (part(3) ? ':' + part(3) : ''); });
    accessor(location, 'pathname', function () { return part(4) || '/'; });
    accessor(location, 'search', function () { return part(5); }, function (v) { dom.wish(1, part(4) + (text(v).charAt(0) === '?' ? '' : '?') + v); });
    accessor(location, 'hash', function () { return part(6); }, function (v) { dom.wish(1, (text(v).charAt(0) === '#' ? '' : '#') + v); });
    accessor(location, 'origin', function () { return part(1) + '//' + this.host; });
    location.assign = location.replace = function (url) { dom.wish(1, text(url)); };
    location.reload = function () { dom.wish(4); };
    location.toString = function () { return dom.url(); };
    accessor(window, 'location', function () { return location; }, function (v) { dom.wish(1, text(v)); });
    accessor(D, 'location', function () { return location; }, function (v) { dom.wish(1, text(v)); });

    window.window = window.self = window.top = window.parent = window.frames = window;
    window.document = document;
    window.addEventListener = ET.addEventListener;
    window.removeEventListener = ET.removeEventListener;
    window.dispatchEvent = ET.dispatchEvent;
    window.attachEvent = E.attachEvent;
    window.detachEvent = E.detachEvent;
    window.name = ''; window.status = ''; window.closed = false; window.length = 0;
    window.opener = null; window.frameElement = null;
    window.navigator = {
        userAgent: 'Mozilla/5.0 (HelloOS; x86_64) HelloOS-Browser', appName: 'Netscape', appCodeName: 'Mozilla',
        appVersion: '5.0 (HelloOS)', platform: 'HelloOS', product: 'Gecko', vendor: '', language: 'de-DE',
        languages: ['de-DE', 'de'], onLine: true, cookieEnabled: true, hardwareConcurrency: 1,
        maxTouchPoints: 0, doNotTrack: '1', plugins: [], mimeTypes: [],
        javaEnabled: function () { return false; },
        sendBeacon: function (url, data) { return !!request('POST', url, {}, data === undefined ? '' : data, 1, 2); }
    };
    window.history = {
        length: 1, state: null, scrollRestoration: 'auto',
        back: function () { dom.wish(3); }, forward: function () {},
        go: function (n) { if (n < 0) dom.wish(3); else if (!n) dom.wish(4); },
        pushState: function (state) { this.state = state; }, replaceState: function (state) { this.state = state; }
    };
    var size = dom.size();
    window.innerWidth = window.outerWidth = size[0];
    window.innerHeight = window.outerHeight = size[1];
    window.screen = { width: size[0], height: size[1], availWidth: size[0], availHeight: size[1], colorDepth: 24, pixelDepth: 24 };
    window.devicePixelRatio = 1;
    window.scrollX = window.pageXOffset = 0;
    window.screenX = window.screenY = window.screenLeft = window.screenTop = 0;
    window.focus = window.blur = window.close = window.stop = window.print = window.moveTo =
        window.resizeTo = function () {};
    /* Rollen: Die Anzeige rollt nur hoch und runter. Wo sie steht, weiß der Browser (dom.scroll). */
    accessor(window, 'scrollY', function () { return dom.scroll(); }, function () {});
    accessor(window, 'pageYOffset', function () { return dom.scroll(); }, function () {});
    var scrollNoted = false;
    window.scrollTo = window.scroll = function (x, y) {
        if (x && typeof x === 'object') y = x.top;
        if (y === undefined || y === null) return;
        var before = dom.scroll();
        if (dom.scroll(+y || 0) === before || scrollNoted) return;
        scrollNoted = true;             /* die Seite erfährt es wie beim Rollen von Hand: danach */
        setTimeout(function () { scrollNoted = false; fire(document, 'scroll'); lookAround(); }, 0);
    };
    window.scrollBy = function (x, y) {
        if (x && typeof x === 'object') y = x.top;
        window.scrollTo(0, dom.scroll() + (+y || 0));
    };
    window.alert = function (message) { dom.alert(text(message)); };
    window.confirm = function (message) { return dom.confirm(text(message)); };
    window.prompt = function () { return null; };
    window.open = function (url) { if (url) dom.wish(1, text(url)); return null; };
    window.getComputedStyle = function (el) { return dom.computed(el); };
    /* Eine Bedingung an die Anzeige: Breite und Höhe des Fensters, Bildschirm, Vorlieben */
    function mediaLength(value) {
        var m = /^(-?[\d.]+)(px|em|rem)?$/.exec(value);
        return m ? parseFloat(m[1]) * (m[2] && m[2] !== 'px' ? 16 : 1) : NaN;
    }
    function mediaPart(part) {
        part = part.trim().toLowerCase();
        if (part === 'all' || part === 'screen' || part === 'only screen' || part === '') return true;
        if (part === 'print' || part === 'speech') return false;
        var m = /^\(\s*(min-|max-)?(width|height)\s*:\s*([^)]+?)\s*\)$/.exec(part);
        if (m) {
            var have = m[2] === 'width' ? window.innerWidth : window.innerHeight, want = mediaLength(m[3]);
            return m[1] === 'min-' ? have >= want : m[1] === 'max-' ? have <= want : have === want;
        }
        m = /^\(\s*(width|height)\s*(<=|>=|<|>)\s*([^)]+?)\s*\)$/.exec(part);
        if (m) {
            have = m[1] === 'width' ? window.innerWidth : window.innerHeight; want = mediaLength(m[3]);
            return m[2] === '<=' ? have <= want : m[2] === '>=' ? have >= want : m[2] === '<' ? have < want : have > want;
        }
        m = /^\(\s*([a-z-]+)\s*(?::\s*([^)]+?)\s*)?\)$/.exec(part);
        if (!m) return false;
        switch (m[1]) {
        case 'orientation': return (m[2] === 'landscape') === (window.innerWidth >= window.innerHeight);
        case 'prefers-color-scheme': return m[2] === 'light';
        case 'prefers-reduced-motion': return m[2] === 'reduce';      /* hier bewegt sich ohnehin nichts */
        case 'hover': case 'any-hover': return m[2] === 'hover';
        case 'pointer': case 'any-pointer': return m[2] === 'fine';
        case 'color': return true;
        default: return false;
        }
    }
    function mediaMatches(query) {
        return String(query).split(',').some(function (one) {
            one = one.trim();
            var negate = /^not\s+/i.test(one);
            if (negate) one = one.replace(/^not\s+/i, '');
            var ok = one.split(/\s+and\s+/i).every(mediaPart);
            return negate ? !ok : ok;
        });
    }
    /*
     * Die Antwort auf eine Bedingung an die Anzeige. Wer zuhört ("change"), erfährt, wenn
     * sie nach einer Größenänderung des Fensters anders ausfällt.
     */
    var mediaLists = [];
    function MediaQueryList(query) {
        this.media = String(query);
        this.onchange = null;
        hide(this, '_was', mediaMatches(query));
    }
    MediaQueryList.prototype = Object.create(EventTarget.prototype);
    MediaQueryList.prototype.constructor = MediaQueryList;
    accessor(MediaQueryList.prototype, 'matches', function () { return mediaMatches(this.media); });
    MediaQueryList.prototype.addListener = function (fn) { this.addEventListener('change', fn); };
    MediaQueryList.prototype.removeListener = function (fn) { this.removeEventListener('change', fn); };
    window.MediaQueryList = MediaQueryList;
    window.matchMedia = function (query) {
        var list = new MediaQueryList(query);
        if (mediaLists.length < 500) mediaLists.push(list);
        return list;
    };
    function mediaChanged() {
        mediaLists.forEach(function (list) {
            var now = mediaMatches(list.media);
            if (now === list._was) return;
            list._was = now;
            var e = new Event('change');
            e.matches = now; e.media = list.media;
            dispatch(list, e);
        });
    }
    window.requestIdleCallback = function (fn) {
        return setTimeout(function () { fn({ didTimeout: false, timeRemaining: function () { return 10; } }); }, 1);
    };
    window.cancelIdleCallback = clearTimeout;
    var started = Date.now();
    window.performance = {
        now: function () { return Date.now() - started; }, timeOrigin: started, timing: { navigationStart: started },
        mark: function () {}, measure: function () {}, getEntriesByType: function () { return []; },
        getEntriesByName: function () { return []; }, clearMarks: function () {}, clearMeasures: function () {}
    };

    function Storage() { hide(this, '_d', Object.create(null)); }
    Storage.prototype.getItem = function (k) { k = String(k); return k in this._d ? this._d[k] : null; };
    Storage.prototype.setItem = function (k, v) { this._d[String(k)] = String(v); };
    Storage.prototype.removeItem = function (k) { delete this._d[String(k)]; };
    Storage.prototype.clear = function () { this._d = Object.create(null); };
    Storage.prototype.key = function (i) { return Object.keys(this._d)[i] || null; };
    accessor(Storage.prototype, 'length', function () { return Object.keys(this._d).length; });
    window.Storage = Storage;
    window.localStorage = new Storage();
    window.sessionStorage = new Storage();

    function observer(name) {
        var C = function () {};
        C.prototype.observe = C.prototype.unobserve = C.prototype.disconnect = function () {};
        C.prototype.takeRecords = function () { return []; };
        window[name] = C;
    }
    observer('MutationObserver'); observer('PerformanceObserver');
    /*
     * "Ist das Element im Bild?" (IntersectionObserver) - gefragt wird meist, um Bilder und
     * Inhalte erst zu laden, wenn sie zu sehen sind. Geprüft wird, wenn der Benutzer rollt
     * und wenn die Seite anders aussieht als zuvor; der Browser gibt dafür Bescheid ("view").
     */
    var watchers = [];
    function watching() {
        dom.watch(watchers.some(function (o) { return o._t.length > 0; }));
    }
    function unwatch(o, el) {
        hide(o, '_t', el === undefined ? [] : o._t.filter(function (t) { return t.el !== el; }));
        if (!o._t.length) watchers = watchers.filter(function (w) { return w !== o; });
        watching();
    }
    function watch(o, el) {
        if (!(el instanceof Element)) throw new TypeError('observe: Argument 1 is not an Element');
        if (o._t.some(function (t) { return t.el === el; })) return false;
        o._t.push({ el: el, step: -1, inside: false, w: -1, h: -1 });
        if (watchers.indexOf(o) < 0) watchers.push(o);
        watching();
        setTimeout(function () { lookAt(o); }, 0);
        return true;
    }
    /* Ränder wie bei margin: ein bis vier Angaben in px oder % */
    function margins(value) {
        var parts = text(value).trim().split(/\s+/).filter(Boolean).map(function (p) {
            if (!/^-?[\d.]+(px|%)?$/.test(p)) throw new SyntaxError('IntersectionObserver: rootMargin must be px or %');
            return { n: parseFloat(p) || 0, percent: /%$/.test(p) };
        });
        if (!parts.length) parts = [{ n: 0, percent: false }];
        if (parts.length > 4) throw new SyntaxError('IntersectionObserver: rootMargin has too many values');
        return [parts[0], parts[1] || parts[0], parts[2] || parts[0], parts[3] || parts[1] || parts[0]];
    }
    function IntersectionObserver(callback, options) {
        if (typeof callback !== 'function') throw new TypeError('IntersectionObserver: callback is not a function');
        options = options || {};
        var m = margins(options.rootMargin);
        var steps = (options.threshold === undefined ? [0] : [].concat(options.threshold)).map(Number);
        steps.forEach(function (t) { if (!(t >= 0 && t <= 1)) throw new RangeError('IntersectionObserver: threshold must be between 0 and 1'); });
        hide(this, '_cb', callback); hide(this, '_t', []); hide(this, '_m', m); hide(this, '_q', []);
        this.root = options.root || null;
        this.rootMargin = m.map(function (p) { return p.n + (p.percent ? '%' : 'px'); }).join(' ');
        this.thresholds = steps.sort(function (a, b) { return a - b; });
    }
    /* die Fläche, in der etwas als "im Bild" gilt - auf der ganzen Seite gemessen */
    function rootArea(o) {
        var x = 0, y = dom.scroll(), w = window.innerWidth, h = window.innerHeight;
        if (o.root && o.root instanceof Element) {
            var r = place(o.root);
            if (!r) return null;
            x = r[0]; y = r[1]; w = r[2]; h = r[3];
        }
        var m = o._m.map(function (p, i) { return p.percent ? p.n * (i % 2 ? w : h) / 100 : p.n; });
        return [x - m[3], y - m[0], w + m[1] + m[3], h + m[0] + m[2]];
    }
    function intersections(o) {
        var area = rootArea(o), top = dom.scroll();
        o._t.forEach(function (t) {
            var known = dom.rect(t.el) !== undefined, r = place(t.el), inside = false, ratio = 0, cut = null;
            if (!known) {                /* niemand setzt die Seite: Was die Gestaltung zeigt, gilt als im Bild */
                inside = !!r; ratio = inside ? 1 : 0; cut = r;
            } else if (r && area) {
                var x1 = Math.max(r[0], area[0]), y1 = Math.max(r[1], area[1]);
                var x2 = Math.min(r[0] + r[2], area[0] + area[2]), y2 = Math.min(r[1] + r[3], area[1] + area[3]);
                if (x2 >= x1 && y2 >= y1) {
                    inside = true;
                    cut = [x1, y1, x2 - x1, y2 - y1];
                    ratio = r[2] * r[3] > 0 ? Math.min(1, cut[2] * cut[3] / (r[2] * r[3])) : 1;
                }
            }
            var step = 0, steps = o.thresholds;
            if (inside) while (step < steps.length && ratio >= steps[step]) step++;
            if (inside && step === 0) inside = false;      /* berührt, aber die erste Schwelle nicht erreicht */
            if (step === t.step && inside === t.inside) return;
            t.step = step; t.inside = inside;
            o._q.push({
                target: t.el, isIntersecting: inside, intersectionRatio: ratio, time: window.performance.now(),
                boundingClientRect: r ? new DOMRect(r[0], r[1] - top, r[2], r[3]) : new DOMRect(0, 0, 0, 0),
                intersectionRect: cut ? new DOMRect(cut[0], cut[1] - top, cut[2], cut[3]) : new DOMRect(0, 0, 0, 0),
                rootBounds: area ? new DOMRect(area[0], area[1] - top, area[2], area[3]) : null
            });
        });
    }
    IntersectionObserver.prototype.observe = function (el) { watch(this, el); };
    IntersectionObserver.prototype.unobserve = function (el) { unwatch(this, el); };
    IntersectionObserver.prototype.disconnect = function () { unwatch(this); this._q.length = 0; };
    IntersectionObserver.prototype.takeRecords = function () { return this._q.splice(0); };
    window.IntersectionObserver = IntersectionObserver;
    window.IntersectionObserverEntry = function IntersectionObserverEntry() {};

    /* "Hat sich die Größe geändert?" (ResizeObserver): dieselbe Prüfung mit den Maßen der Elemente */
    function ResizeObserver(callback) {
        if (typeof callback !== 'function') throw new TypeError('ResizeObserver: callback is not a function');
        hide(this, '_cb', callback); hide(this, '_t', []); hide(this, '_q', []); hide(this, '_sizes', true);
    }
    function sizes(o) {
        o._t.forEach(function (t) {
            var r = place(t.el), w = r ? r[2] : 0, h = r ? r[3] : 0;
            if (w === t.w && h === t.h) return;
            t.w = w; t.h = h;
            var iw = r ? Math.max(0, w - r[5] - r[7] - r[10] - r[12]) : 0, ih = r ? Math.max(0, h - r[4] - r[6] - r[9] - r[11]) : 0;
            o._q.push({ target: t.el, contentRect: new DOMRect(r ? r[12] : 0, r ? r[9] : 0, iw, ih),
                        borderBoxSize: [{ inlineSize: w, blockSize: h }], contentBoxSize: [{ inlineSize: iw, blockSize: ih }],
                        devicePixelContentBoxSize: [{ inlineSize: iw, blockSize: ih }] });
        });
    }
    ResizeObserver.prototype.observe = function (el) { watch(this, el); };
    ResizeObserver.prototype.unobserve = function (el) { unwatch(this, el); };
    ResizeObserver.prototype.disconnect = function () { unwatch(this); this._q.length = 0; };
    window.ResizeObserver = ResizeObserver;
    window.ResizeObserverEntry = function ResizeObserverEntry() {};

    function lookAt(o) {
        if (o._sizes) sizes(o); else intersections(o);
        var records = o._q.splice(0);
        if (!records.length) return;
        try { o._cb.call(o, records, o); } catch (e) { report(e); }
    }
    /* alle Beobachter prüfen: nach dem Rollen und wenn die Seite neu gesetzt wurde */
    function lookAround() {
        watchers.slice().forEach(lookAt);
    }

    /* Fehler der Seitenschnittstellen */
    var domErrors = { IndexSizeError: 1, HierarchyRequestError: 3, WrongDocumentError: 4, InvalidCharacterError: 5,
                      NoModificationAllowedError: 7, NotFoundError: 8, NotSupportedError: 9, InvalidStateError: 11,
                      SyntaxError: 12, InvalidModificationError: 13, NamespaceError: 14, InvalidAccessError: 15,
                      SecurityError: 18, NetworkError: 19, AbortError: 20, QuotaExceededError: 22, TimeoutError: 23,
                      DataCloneError: 25 };
    function DOMException(message, name) {
        this.message = message === undefined ? '' : String(message);
        this.name = name === undefined ? 'Error' : String(name);
        this.code = domErrors[this.name] || 0;
        this.stack = new Error(this.message).stack;
    }
    DOMException.prototype = Object.create(Error.prototype);
    DOMException.prototype.constructor = DOMException;
    window.DOMException = DOMException;

    window.Image = function (width, height) {
        var img = dom.el('img');
        if (width !== undefined) dom.setAttr(img, 'width', String(width));
        if (height !== undefined) dom.setAttr(img, 'height', String(height));
        return img;
    };
    window.Option = function (label, value) {
        var option = dom.el('option');
        if (label !== undefined) dom.setText(option, text(label));
        if (value !== undefined) dom.setAttr(option, 'value', text(value));
        return option;
    };

    var b64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
    window.btoa = function (input) {
        var s = String(input), out = '';
        for (var i = 0; i < s.length; i += 3) {
            var a = s.charCodeAt(i), b = s.charCodeAt(i + 1), c = s.charCodeAt(i + 2);
            if (a > 255 || b > 255 || c > 255) throw new Error('InvalidCharacterError');
            out += b64.charAt(a >> 2) + b64.charAt(((a & 3) << 4) | (b >> 4)) +
                   (i + 1 < s.length ? b64.charAt(((b & 15) << 2) | (c >> 6)) : '=') +
                   (i + 2 < s.length ? b64.charAt(c & 63) : '=');
        }
        return out;
    };
    window.atob = function (input) {
        var s = String(input).replace(/[\s=]+/g, ''), out = '', bits = 0, count = 0;
        for (var i = 0; i < s.length; i++) {
            var v = b64.indexOf(s.charAt(i));
            if (v < 0) throw new Error('InvalidCharacterError');
            bits = (bits << 6) | v;
            count += 6;
            if (count >= 8) { count -= 8; out += String.fromCharCode((bits >> count) & 255); }
        }
        return out;
    };

    function URLSearchParams(init) {
        hide(this, '_p', []);
        var s = text(init);
        if (init && typeof init === 'object') {
            if (typeof init[Symbol.iterator] === 'function') {
                for (var pair of init) this._p.push([String(pair[0]), String(pair[1])]);
            } else {
                for (var k in init) this._p.push([k, String(init[k])]);
            }
            return;
        }
        if (s.charAt(0) === '?') s = s.slice(1);
        s.split('&').forEach(function (pair) {
            if (!pair) return;
            var i = pair.indexOf('='), dec = function (t) { try { return decodeURIComponent(t.replace(/\+/g, ' ')); } catch (e) { return t; } };
            this._p.push(i < 0 ? [dec(pair), ''] : [dec(pair.slice(0, i)), dec(pair.slice(i + 1))]);
        }, this);
    }
    var USP = URLSearchParams.prototype;
    USP.get = function (k) { for (var i = 0; i < this._p.length; i++) if (this._p[i][0] === String(k)) return this._p[i][1]; return null; };
    USP.getAll = function (k) { return this._p.filter(function (p) { return p[0] === String(k); }).map(function (p) { return p[1]; }); };
    USP.has = function (k) { return this.get(k) !== null; };
    USP.append = function (k, v) { this._p.push([String(k), String(v)]); };
    USP['delete'] = function (k) { this._p = this._p.filter(function (p) { return p[0] !== String(k); }); };
    USP.set = function (k, v) { this['delete'](k); this.append(k, v); };
    USP.forEach = function (fn, self) { this._p.forEach(function (p) { fn.call(self, p[1], p[0], this); }, this); };
    USP.keys = function () { return this._p.map(function (p) { return p[0]; })[Symbol.iterator](); };
    USP.values = function () { return this._p.map(function (p) { return p[1]; })[Symbol.iterator](); };
    USP.entries = function () { return this._p.map(function (p) { return [p[0], p[1]]; })[Symbol.iterator](); };
    hide(USP, Symbol.iterator, USP.entries);
    USP.toString = function () {
        return this._p.map(function (p) { return encodeURIComponent(p[0]) + '=' + encodeURIComponent(p[1]); }).join('&');
    };
    window.URLSearchParams = URLSearchParams;

    /* --- Daten nachladen: XMLHttpRequest und fetch ------------------------------------------ */

    /* Die Felder eines Formulars, wie es sie absenden würde */
    function FormData(form) {
        hide(this, '_p', []);
        if (!form || dom.name(form) !== 'form') return;
        var fields = form.elements;
        for (var i = 0; i < fields.length; i++) {
            var f = fields[i], n = dom.name(f), type = inputType(f), key = dom.attr(f, 'name');
            if (!key || dom.attr(f, 'disabled') !== null) continue;
            if (n === 'button' || type === 'submit' || type === 'button' || type === 'reset' || type === 'image' || type === 'file') continue;
            if ((type === 'checkbox' || type === 'radio') && !dom.checked(f)) continue;
            this._p.push([key, type === 'checkbox' || type === 'radio' ? (dom.attr(f, 'value') === null ? 'on' : dom.attr(f, 'value')) : text(f.value)]);
        }
    }
    ['get', 'getAll', 'has', 'append', 'delete', 'set', 'forEach', 'keys', 'values', 'entries', Symbol.iterator].forEach(function (name) {
        hide(FormData.prototype, name, USP[name]);
    });
    window.FormData = FormData;

    function Headers(init) {
        hide(this, '_h', {});
        if (init instanceof Headers) { for (var k in init._h) this._h[k] = init._h[k]; }
        else if (init && typeof init[Symbol.iterator] === 'function') { for (var p of init) this.append(p[0], p[1]); }
        else if (init && typeof init === 'object') { for (var name in init) this.append(name, init[name]); }
    }
    Headers.prototype.append = function (k, v) { k = String(k).toLowerCase(); this._h[k] = k in this._h ? this._h[k] + ', ' + v : String(v); };
    Headers.prototype.set = function (k, v) { this._h[String(k).toLowerCase()] = String(v); };
    Headers.prototype.get = function (k) { k = String(k).toLowerCase(); return k in this._h ? this._h[k] : null; };
    Headers.prototype.has = function (k) { return String(k).toLowerCase() in this._h; };
    Headers.prototype['delete'] = function (k) { delete this._h[String(k).toLowerCase()]; };
    Headers.prototype.forEach = function (fn, self) { for (var k in this._h) fn.call(self, this._h[k], k, this); };
    Headers.prototype.keys = function () { return Object.keys(this._h).sort()[Symbol.iterator](); };
    Headers.prototype.values = function () { var h = this._h; return Object.keys(h).sort().map(function (k) { return h[k]; })[Symbol.iterator](); };
    Headers.prototype.entries = function () { var h = this._h; return Object.keys(h).sort().map(function (k) { return [k, h[k]]; })[Symbol.iterator](); };
    hide(Headers.prototype, Symbol.iterator, Headers.prototype.entries);
    window.Headers = Headers;

    var statusNames = { 200: 'OK', 201: 'Created', 204: 'No Content', 301: 'Moved Permanently', 302: 'Found',
                        304: 'Not Modified', 400: 'Bad Request', 401: 'Unauthorized', 403: 'Forbidden',
                        404: 'Not Found', 500: 'Internal Server Error', 502: 'Bad Gateway', 503: 'Service Unavailable' };

    /*
     * Stellt die Anfrage; liefert { status, type, text, url, headers, kind,
     * redirected } oder null. credentials: 0 ohne Cookies, 1 nur zum Server der
     * Seite, 2 immer; mode: 0 auch fremde Server (mit deren Erlaubnis), 1 nur
     * der eigene, 2 fremde ohne Einblick in die Antwort. Die Regeln dafür
     * setzt der Browser durch, nicht dieses Skript.
     */
    function request(method, url, headers, body, credentials, mode) {
        var lines = [], hasType = false, payload = null;
        for (var k in headers) { lines.push(k + ': ' + headers[k]); if (k.toLowerCase() === 'content-type') hasType = true; }
        if (body !== null && body !== undefined && method !== 'GET' && method !== 'HEAD') {
            if (body instanceof FormData || body instanceof URLSearchParams) {
                payload = USP.toString.call(body);
                if (!hasType) lines.push('Content-Type: application/x-www-form-urlencoded;charset=UTF-8');
            } else {
                payload = String(body);
                if (!hasType) lines.push('Content-Type: text/plain;charset=UTF-8');
            }
        }
        var full = dom.resolve(text(url));
        var reply = dom.request(method, full, lines.join('\n'), payload,
                                credentials === undefined ? 1 : credentials, mode || 0);
        return reply ? { status: reply[0], type: reply[1], text: reply[2], url: reply[3], headers: reply[4],
                         kind: reply[5], redirected: reply[6] } : null;
    }
    /* Die Kopfzeilen einer Antwort ("name: wert" je Zeile) als Headers */
    function replyHeaders(lines) {
        var h = new Headers();
        text(lines).split('\n').forEach(function (line) {
            var i = line.indexOf(':');
            if (i > 0) h.append(line.slice(0, i), line.slice(i + 1).trim());
        });
        return h;
    }

    function XMLHttpRequest() {
        hide(this, '_m', 'GET'); hide(this, '_u', ''); hide(this, '_h', {}); hide(this, '_sync', false);
        hide(this, '_run', 0); hide(this, '_type', '');
        this.readyState = 0; this.status = 0; this.statusText = ''; this.responseText = ''; this.response = '';
        this.responseType = ''; this.responseURL = ''; this.responseXML = null; this.timeout = 0;
        this.withCredentials = false;
        this.upload = new EventTarget();
    }
    var X = XMLHttpRequest.prototype = Object.create(ET);
    X.constructor = XMLHttpRequest;
    XMLHttpRequest.UNSENT = X.UNSENT = 0; XMLHttpRequest.OPENED = X.OPENED = 1;
    XMLHttpRequest.HEADERS_RECEIVED = X.HEADERS_RECEIVED = 2; XMLHttpRequest.LOADING = X.LOADING = 3;
    XMLHttpRequest.DONE = X.DONE = 4;
    function xhrState(x, state) { x.readyState = state; fire(x, 'readystatechange', false); }
    X.open = function (method, url, async) {
        this._m = String(method).toUpperCase(); this._u = text(url); this._h = {}; this._sync = async === false;
        this._run++;
        this.status = 0; this.statusText = ''; this.responseText = ''; this.response = '';
        xhrState(this, 1);
    };
    X.setRequestHeader = function (name, value) {
        if (this.readyState !== 1) throw new DOMException('The object is in an invalid state.', 'InvalidStateError');
        name = String(name);
        this._h[name] = name in this._h ? this._h[name] + ', ' + value : String(value);
    };
    X.overrideMimeType = function () {};
    X.getResponseHeader = function (name) {
        return this.readyState >= 2 && this._heads ? this._heads.get(name) : null;
    };
    X.getAllResponseHeaders = function () {
        var out = '';
        if (this.readyState >= 2 && this._heads) this._heads.forEach(function (v, k) { out += k + ': ' + v + '\r\n'; });
        return out;
    };
    X.abort = function () {
        this._run++;
        if (this.readyState > 1 && this.readyState < 4) { this.readyState = 4; fire(this, 'abort', false); fire(this, 'loadend', false); }
        this.readyState = 0;
    };
    X.send = function (body) {
        if (this.readyState !== 1) throw new DOMException('The object is in an invalid state.', 'InvalidStateError');
        var self = this, run = ++this._run;
        var reply = request(this._m, this._u, this._h, body, this.withCredentials ? 2 : 1, 0);
        function finish() {
            if (self._run !== run) return;          /* abgebrochen oder neu geöffnet */
            if (!reply) {
                self.status = 0;
                xhrState(self, 4);
                fire(self, 'error', false); fire(self, 'loadend', false);
                return;
            }
            self.status = reply.status; self.statusText = statusNames[reply.status] || '';
            self._type = reply.type; self.responseURL = reply.url;
            hide(self, '_heads', replyHeaders(reply.headers));
            xhrState(self, 2); xhrState(self, 3);
            self.responseText = reply.text;
            self.response = reply.text;
            if (self.responseType === 'json') { try { self.response = JSON.parse(reply.text); } catch (e) { self.response = null; } }
            xhrState(self, 4);
            fire(self, 'load', false); fire(self, 'loadend', false);
        }
        if (this._sync) finish(); else setTimeout(finish, 0);
    };
    window.XMLHttpRequest = XMLHttpRequest;

    function Response(body, init) {
        hide(this, '_t', text(body));
        this.status = init && init.status !== undefined ? init.status : 200;
        this.statusText = init && init.statusText !== undefined ? init.statusText : statusNames[this.status] || '';
        this.ok = this.status >= 200 && this.status < 300;
        this.headers = new Headers(init && init.headers);
        this.url = init && init.url || ''; this.redirected = false; this.type = 'basic'; this.bodyUsed = false;
    }
    Response.prototype.text = function () { this.bodyUsed = true; return Promise.resolve(this._t); };
    Response.prototype.json = function () { var t = this._t; this.bodyUsed = true; return new Promise(function (ok) { ok(JSON.parse(t)); }); };
    /* als Bytes: der Text in UTF-8 (echte Binärdaten kommen hier nicht unversehrt an) */
    Response.prototype.arrayBuffer = function () {
        var t = this._t; this.bodyUsed = true;
        return new Promise(function (ok) { ok(new TextEncoder().encode(t).buffer); });
    };
    Response.prototype.clone = function () { return new Response(this._t, { status: this.status, statusText: this.statusText, headers: this.headers, url: this.url }); };
    window.Response = Response;

    window.fetch = function (input, init) {
        return new Promise(function (resolve, reject) {
            var url = typeof input === 'string' ? input : input && input.url !== undefined ? input.url : text(input);
            var method = String(init && init.method || input && input.method || 'GET').toUpperCase();
            var headers = {}, given = new Headers(init && init.headers || input && input.headers);
            given.forEach(function (v, k) { headers[k] = v; });
            var want = init && init.credentials || input && input.credentials;
            var mode = init && init.mode || input && input.mode;
            var signal = init && init.signal;
            if (signal && signal.aborted) { reject(new DOMException('The operation was aborted.', 'AbortError')); return; }
            var reply = request(method, url, headers, init && init.body !== undefined ? init.body : input && input._body,
                                want === 'omit' ? 0 : want === 'include' ? 2 : 1,
                                mode === 'same-origin' ? 1 : mode === 'no-cors' ? 2 : 0);
            if (!reply) { reject(new TypeError('Failed to fetch')); return; }
            var response = new Response(reply.text, { status: reply.status, headers: replyHeaders(reply.headers), url: reply.url });
            response.type = reply.kind;
            response.redirected = reply.redirected;
            if (reply.kind === 'opaque') { response.ok = false; response.statusText = ''; }
            resolve(response);
        });
    };
    function AbortController() { this.signal = new EventTarget(); this.signal.aborted = false; }
    AbortController.prototype.abort = function () { this.signal.aborted = true; fire(this.signal, 'abort', false); };
    window.AbortController = AbortController;

    function URL(url, base) {
        var href = dom.resolve(text(url), base === undefined ? undefined : text(base));
        var m = /^([a-z][a-z0-9+.\-]*:)\/\/([^\/?#:]*)(?::(\d+))?([^?#]*)(\?[^#]*)?(#.*)?$/i.exec(href);
        if (!m) throw new TypeError('Invalid URL');
        this.protocol = m[1]; this.hostname = m[2]; this.port = m[3] || '';
        this.host = m[2] + (m[3] ? ':' + m[3] : ''); this.pathname = m[4] || '/';
        this.search = m[5] || ''; this.hash = m[6] || (/#/.test(text(url)) ? text(url).slice(text(url).indexOf('#')) : '');
        this.origin = m[1] + '//' + this.host; this.username = this.password = '';
        this.searchParams = new URLSearchParams(this.search);
        this.href = this.origin + this.pathname + this.search + this.hash;
    }
    URL.prototype.toString = URL.prototype.toJSON = function () { return this.href; };
    URL.createObjectURL = function () { return ''; };
    URL.revokeObjectURL = function () {};
    window.URL = URL;

    /* --- Eintritt für den Browser ---------------------------------------------------------- */

    var keyCodes = { Enter: 13, Backspace: 8, Tab: 9, Escape: 27, ' ': 32, ArrowLeft: 37, ArrowUp: 38,
                     ArrowRight: 39, ArrowDown: 40, Delete: 46 };
    function keyEvent(type, key) {
        var code = keyCodes[key] || (key.length === 1 ? key.toUpperCase().charCodeAt(0) : 0);
        if (type === 'keypress' && key.length === 1) code = key.charCodeAt(0);
        var e = new window.KeyboardEvent(type, { bubbles: true, cancelable: true, key: key, keyCode: code,
                                                 which: code, charCode: type === 'keypress' ? code : 0,
                                                 code: key.length === 1 ? 'Key' + key.toUpperCase() : key });
        e.isTrusted = true;
        return e;
    }
    /* Wo der Mauszeiger steht (im sichtbaren Ausschnitt) - der Browser meldet es mit jedem Wechsel */
    var pointerX = 0, pointerY = 0;
    function mouseEvent(type, bubbles, related) {
        var e = new MouseEvent(type, { bubbles: bubbles, cancelable: bubbles, clientX: pointerX, clientY: pointerY,
                                       pageX: pointerX, pageY: pointerY + dom.scroll(), screenX: pointerX,
                                       screenY: pointerY, offsetX: 0, offsetY: 0, relatedTarget: related || null,
                                       view: window });
        e.isTrusted = true;
        return e;
    }
    function ancestors(node) {
        var list = [];
        for (var n = node, guard = 0; n && dom.type(n) === 1 && guard < 2000; n = dom.parent(n), guard++) list.push(n);
        return list;
    }
    /*
     * Der Zeiger ist von einem Element auf ein anderes gewandert: mouseout und mouseover steigen
     * auf wie ein Klick; mouseleave und mouseenter gelten je Element, das wirklich verlassen
     * bzw. betreten wurde (nicht, wenn der Zeiger nur in ein Kind wechselt).
     */
    function pointerChanged(now, before) {
        var left = before ? ancestors(before) : [], entered = now ? ancestors(now) : [];
        if (before) {
            dispatch(before, mouseEvent('pointerout', true, now));
            dispatch(before, mouseEvent('mouseout', true, now));
            left.forEach(function (el) {
                if (entered.indexOf(el) < 0) { dispatch(el, mouseEvent('pointerleave', false, now)); dispatch(el, mouseEvent('mouseleave', false, now)); }
            });
        }
        if (now) {
            dispatch(now, mouseEvent('pointerover', true, before));
            dispatch(now, mouseEvent('mouseover', true, before));
            entered.reverse().forEach(function (el) {
                if (left.indexOf(el) < 0) { dispatch(el, mouseEvent('pointerenter', false, before)); dispatch(el, mouseEvent('mouseenter', false, before)); }
            });
        }
    }
    /* Ein Klick des Benutzers; true: Der Browser soll tun, was der Klick sonst bewirkt (Link, Absenden). */
    function userClick(node) {
        ['pointerdown', 'mousedown', 'pointerup', 'mouseup'].forEach(function (type) {
            dispatch(node, mouseEvent(type, true, null));
        });
        var click = mouseEvent('click', true, null);
        var name = dom.name(node), kind = name === 'input' || name === 'button' ? inputType(node) : '';
        if (dom.type(node) === 1 && dom.attr(node, 'disabled') !== null && (name === 'input' || name === 'button')) return false;
        if (name === 'input' && (kind === 'checkbox' || kind === 'radio')) {
            clickCheckable(node, click);
            return false;
        }
        if (!dispatch(node, click)) return false;
        /* <details>: Ein Klick auf die Überschrift klappt auf und zu. */
        var summary = dom.type(node) === 1 ? node.closest('summary') : null, details = summary && dom.parent(summary);
        if (details && dom.name(details) === 'details' && !node.closest('a, button, input, select, textarea')) {
            if (dom.attr(details, 'open') === null) dom.setAttr(details, 'open', ''); else dom.delAttr(details, 'open');
            fire(details, 'toggle', false);
            return false;
        }
        if (kind === 'reset') {
            var form = node.form;
            if (form) form.reset();
            return false;
        }
        /* Ein Klick auf die Beschriftung gilt ihrem Ankreuzfeld. */
        var label = dom.type(node) === 1 ? node.closest('label') : null;
        if (label && !node.closest('a, button, input, select, textarea')) {
            var id = dom.attr(label, 'for');
            var field = id !== null ? document.getElementById(id) : label.querySelector('input');
            if (field && dom.name(field) === 'input' && (inputType(field) === 'checkbox' || inputType(field) === 'radio')) {
                field.click();
                return false;
            }
        }
        return true;
    }

    dom.dispatcher(function (node, type, detail, info) {
        if (type === 'DOMContentLoaded') {
            ready = 'interactive';
            fire(document, 'readystatechange', false);
            return fire(document, 'DOMContentLoaded');
        }
        if (type === 'load' && node === null) {
            ready = 'complete';
            fire(document, 'readystatechange', false);
            return fire(window, 'load', false);
        }
        if (type === 'view') {            /* der Ausschnitt oder das Aussehen der Seite hat sich geändert */
            if (info === 'resize') {
                var size = dom.size();
                window.innerWidth = window.outerWidth = size[0];
                window.innerHeight = window.outerHeight = size[1];
                mediaChanged();
                fire(window, 'resize', false);
            }
            if (info === 'scroll') fire(document, 'scroll');
            lookAround();
            return true;
        }
        if (type === 'pointer' || type === 'mousemove') {
            var at = String(info || '').split(',');
            pointerX = +at[0] || 0; pointerY = +at[1] || 0;
            if (type === 'pointer') pointerChanged(node, detail);
            else if (node) { dispatch(node, mouseEvent('pointermove', true, null)); dispatch(node, mouseEvent('mousemove', true, null)); }
            return true;
        }
        if (type === 'click') return userClick(node);
        if (type === 'submit') {
            var submit = new Event('submit', { bubbles: true, cancelable: true });
            submit.submitter = detail || null;
            return dispatch(node, submit);
        }
        if (type === 'keydown' || type === 'keyup' || type === 'keypress') return dispatch(node, keyEvent(type, String(info || '')));
        return fire(node, type, type !== 'load' && type !== 'error' && type !== 'focus' && type !== 'blur',
                    type === 'click' || type === 'submit');
    });
})(this);
