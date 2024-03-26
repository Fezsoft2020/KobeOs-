#include <web-base/namespaces.h>
#include <web-dom/comment.h>
#include <web-dom/document-type.h>

#include "parser.h"

namespace Web::Html {

// 13.2.2 Parse errors
// https://html.spec.whatwg.org/multipage/parsing.html#parse-errors

void Parser::_raise(Str msg) {
    logError("{}: {}", toStr(_insertionMode), msg);
}

// 13.2.5 Tokenization
// https://html.spec.whatwg.org/multipage/parsing.html#tokenization

// https://html.spec.whatwg.org/multipage/parsing.html#acknowledge-self-closing-flag
void acknowledgeSelfClosingFlag(Token const &) {
    logTodo();
}

// 13.2.6 Tree construction
// https://html.spec.whatwg.org/multipage/parsing.html#tree-construction

// 13.2.6.1 Creating and inserting nodes
// https://html.spec.whatwg.org/multipage/parsing.html#creating-and-inserting-nodes

struct AdjustedInsertionLocation {
    Strong<Dom::Element> parent;

    // https://html.spec.whatwg.org/multipage/parsing.html#insert-an-element-at-the-adjusted-insertion-location
    void insert(Strong<Dom::Node> node) {
        // NOSPEC
        parent->appendChild(node);
    }

    Opt<Strong<Dom::Node>> lastChild() {
        // NOSPEC
        if (not parent->hasChildren())
            return NONE;
        return parent->lastChild();
    }
};

// https://html.spec.whatwg.org/multipage/parsing.html#appropriate-place-for-inserting-a-node
AdjustedInsertionLocation apropriatePlaceForInsertingANode(Parser &b, Opt<Strong<Dom::Element>> overrideTarget = NONE) {
    // 1. If there was an override target specified, then let target be
    //    the override target.
    //
    //    Otherwise, let target be the current node.
    auto target = overrideTarget
                      ? *overrideTarget
                      : last(b._openElements);

    // 2. Determine the adjusted insertion location using the first
    //    matching steps from the following list:

    //    If foster parenting is enabled and target is a table, tbody, tfoot, thead, or tr element

    //    NOTE: Foster parenting happens when content is misnested in tables.

    //    1. Let last template be the last template element in the stack of open elements, if any.

    //    2. Let last table be the last table element in the stack of open elements, if any.

    //    3. If there is a last template and either there is no last table,
    //       or there is one, but last template is lower (more recently added)
    //       than last table in the stack of open elements,
    //       then: let adjusted insertion location be inside last template's
    //       template contents, after its last child (if any), and abort these steps.

    //    4. If there is no last table, then let adjusted insertion location be
    //       inside the first element in the stack of open elements (the html element),
    //       after its last child (if any), and abort these steps. (fragment case)

    //    5. If last table has a parent node, then let adjusted insertion location
    //       be inside last table's parent node, immediately before last table,
    //       and abort these steps.

    //    6. Let previous element be the element immediately above last table
    //       in the stack of open elements.

    //    7. Let adjusted insertion location be inside previous element,
    //       after its last child (if any).

    //  Otherwise: Let adjusted insertion location be inside target,
    //             after its last child (if any).

    // 3. If the adjusted insertion location is inside a template element,
    //    let it instead be inside the template element's template contents,
    //    after its last child (if any).

    // 4. Return the adjusted insertion location.
    return {target};
}

// https://html.spec.whatwg.org/multipage/parsing.html#create-an-element-for-the-token
Strong<Dom::Element> createElementFor(Token const &t, Namespace) {
    auto el = makeStrong<Dom::Element>(t.name.unwrap());
    // NOSPEC: Keep it simple for the POC

    // 1. If the active speculative HTML parser is not null, then return the
    //    result of creating a speculative mock element given given namespace,
    //    the tag name of the given token, and the attributes of the given token.

    // 2. Otherwise, optionally create a speculative mock element given given
    //    namespace, the tag name of the given token, and the attributes of
    //    the given token.

    // 3. Let document be intended parent's node document.

    // 4. Let local name be the tag name of the token

    // 5. Let is be the value of the "is" attribute in the given token, if
    //    such an attribute exists, or null otherwise.

    // 6. Let definition be the result of looking up a custom element
    //    definition given document, given namespace, local name, and is.

    // 7. If definition is non-null and the parser was not created as part
    //    of the HTML fragment parsing algorithm, then let will execute
    //    script be true. Otherwise, let it be false.

    // NOSPEC: We don't support scripting so we don't need to worry about this
    bool willExecuteScript = false;

    // 8. If will execute script is true, then:

    if (willExecuteScript) {
        // 1. Increment document's throw-on-dynamic-markup-insertion counter.

        // 2. If the JavaScript execution context stack is empty,
        //    then perform a microtask checkpoint.

        // 3. Push a new element queue onto document's relevant agent's
        //    custom element reactions stack.
    }

    // 9. Let element be the result of creating an element given document,
    //    localName, given namespace, null, and is. If will execute script
    //    is true, set the synchronous custom elements flag; otherwise,
    //    leave it unset.

    // 10. Append each attribute in the given token to element.

    // 11. If will execute script is true, then:
    if (willExecuteScript) {
        // 1. Let queue be the result of popping from document's relevant
        //    agent's custom element reactions stack. (This will be the
        //    same element queue as was pushed above.)

        // 2. Invoke custom element reactions in queue.

        // 3. Decrement document's throw-on-dynamic-markup-insertion counter.
    }

    // 12. If element has an xmlns attribute in the XMLNS namespace whose
    //     value is not exactly the same as the element's namespace, that
    //     is a parse error. Similarly, if element has an xmlns:xlink
    //     attribute in the XMLNS namespace whose value is not the XLink
    //     Namespace, that is a parse error.

    // 13. If element is a resettable element, invoke its reset algorithm.
    //     (This initializes the element's value and checkedness based on the element's attributes.)

    // 14. If element is a form-associated element and not a form-associated
    //     custom element, the form element pointer is not null, there is no
    //     template element on the stack of open elements, element is either
    //     not listed or doesn't have a form attribute, and the intended parent
    //     is in the same tree as the element pointed to by the form element pointer,

    //     Then associate element with the form element pointed to by the form
    //     element pointer and set element's parser inserted flag.

    // 15. Return element.
    return el;
}

// https://html.spec.whatwg.org/multipage/parsing.html#insert-a-foreign-element

static Strong<Dom::Element> insertAForeignElement(Parser &b, Token const &t, Namespace ns, bool onlyAddToElementStack = false) {
    // 1. Let the adjusted insertion location be the appropriate place for inserting a node.
    auto location = apropriatePlaceForInsertingANode(b);

    // 2. Let element be the result of creating an element for the token in the
    // given namespace, with the intended parent being the element in which the
    // adjusted insertion location finds itself.
    auto el = createElementFor(t, ns);

    // 3. If onlyAddToElementStack is false, then run insert an element at the adjusted insertion location with element.
    if (not onlyAddToElementStack) {
        location.insert(el);
    }

    // 4. Push element onto the stack of open elements so that it is the new current node.
    b._openElements.pushBack(el);

    // 5. Return element.
    return el;
}

// https://html.spec.whatwg.org/multipage/parsing.html#insert-an-html-element
static Strong<Dom::Element> insertHtmlElement(Parser &b, Token const &t) {
    return insertAForeignElement(b, t, Namespace::HTML, false);
}

// https://html.spec.whatwg.org/multipage/parsing.html#insert-a-character
static void insertACharacter(Parser &b, Rune c) {
    // 2. Let the adjusted insertion location be the appropriate place for inserting a node.
    auto location = apropriatePlaceForInsertingANode(b);

    // 3. If the adjusted insertion location is inside a Document node, then ignore the token.
    if (location.parent->nodeType() == Dom::NodeType::DOCUMENT)
        return;

    // 4. If there is a Text node immediately before the adjusted insertion
    //    location, then append data to that Text node's data.
    auto lastChild = location.lastChild();
    if (lastChild and (*lastChild)->nodeType() == Dom::NodeType::TEXT) {
        auto text = (*(*lastChild).cast<Dom::Text>());
        text->appendData(c);
    }

    // Otherwise, create a new Text node whose data is data and whose node
    //            document is the same as that of the element in which the
    //            adjusted insertion location finds itself, and insert the
    //            newly created node at the adjusted insertion location.
    else {
        auto text = makeStrong<Dom::Text>("");
        text->appendData(c);

        location.insert(text);
    }
}

static void insertACharacter(Parser &b, Token const &t) {
    // 1. Let data be the characters passed to the algorithm, or, if no characters were explicitly specified, the character of the character token being processed.
    insertACharacter(b, t.rune.unwrap());
}

// https://html.spec.whatwg.org/multipage/parsing.html#insert-a-comment
static void insertAComment(Parser &b, Token const &t) {
    // 1. Let data be the data given in the comment token being processed.

    // 2. If position was specified, then let the adjusted insertion
    //    location be position. Otherwise, let adjusted insertion location
    //    be the appropriate place for inserting a node.

    // TODO: If position was
    auto location = apropriatePlaceForInsertingANode(b);

    // 3. Create a Comment node whose data attribute is set to data and
    //    whose node document is the same as that of the node in which
    //    the adjusted insertion location finds itself.
    auto comment = makeStrong<Dom::Comment>(t.data.unwrap());

    // 4. Insert the newly created node at the adjusted insertion location.
    location.insert(comment);
}

// 13.2.6.2 Parsing elements that contain only text
// https://html.spec.whatwg.org/multipage/parsing.html#parsing-elements-that-contain-only-text

static void parseRawTextElement(Parser &b, Token const &t) {
    insertHtmlElement(b, t);
    b._lexer._switchTo(Lexer::RAWTEXT);
    b._originalInsertionMode = b._insertionMode;
    b._switchTo(Parser::Mode::TEXT);
}

static void parseRcDataElement(Parser &b, Token const &t) {
    insertHtmlElement(b, t);
    b._lexer._switchTo(Lexer::RCDATA);
    b._originalInsertionMode = b._insertionMode;
    b._switchTo(Parser::Mode::TEXT);
}

// 13.2.6.4 The rules for parsing tokens in HTML content

// 13.2.6.4.1 The "initial" insertion mode
// https://html.spec.whatwg.org/multipage/parsing.html#the-initial-insertion-mode

static Dom::QuirkMode _whichQuirkMode(Token const &) {
    // NOSPEC: We assume no quirk mode
    return Dom::QuirkMode::NO;
}

void Parser::_handleInitialMode(Token const &t) {
    // A character token that is one of U+0009 CHARACTER TABULATION,
    // U+000A LINE FEED (LF), U+000C FORM FEED (FF),
    // U+000D CARRIAGE RETURN (CR), or U+0020 SPACE
    if (t.type == Token::CHARACTER and
        (t.rune == '\t' or
         t.rune == '\n' or
         t.rune == '\f' or
         t.rune == ' ')) {
        // ignore
    }

    // A comment token
    else if (t.type == Token::COMMENT) {
        _document->appendChild(makeStrong<Dom::Comment>(t.data.unwrap()));
    }

    // A DOCTYPE token
    else if (t.type == Token::DOCTYPE) {
        _document->appendChild(makeStrong<Dom::DocumentType>(
            tryOr(t.name, ""),
            tryOr(t.publicIdent, ""),
            tryOr(t.systemIdent, "")
        ));
        _document->quirkMode = _whichQuirkMode(t);
        _switchTo(Mode::BEFORE_HTML);
    }

    // Anything else
    else {
        _raise();
        _switchTo(Mode::BEFORE_HTML);
        accept(t);
    }
}

// https://html.spec.whatwg.org/multipage/parsing.html#the-before-html-insertion-mode
void Parser::_handleBeforeHtml(Token const &t) {
    // A DOCTYPE token
    if (t.type == Token::DOCTYPE) {
        // ignore
        _raise();
    }

    // A comment token
    else if (t.type == Token::COMMENT) {
        _document->appendChild(makeStrong<Dom::Comment>(t.data.unwrap()));
    }

    // A character token that is one of U+0009 CHARACTER TABULATION,
    // U+000A LINE FEED (LF), U+000C FORM FEED (FF),
    // U+000D CARRIAGE RETURN (CR), or U+0020 SPACE
    if (t.type == Token::CHARACTER and
        (t.rune == '\t' or
         t.rune == '\n' or
         t.rune == '\f' or
         t.rune == ' ')) {
        // ignore
    }

    // A start tag whose tag name is "html"
    else if (t.type == Token::START_TAG and t.name == "html") {
        auto el = createElementFor(t, Namespace::HTML);
        _document->appendChild(el);
        _openElements.pushBack(el);
        _switchTo(Mode::BEFORE_HEAD);
    }

    // Any other end tag
    else if (t.type == Token::END_TAG and not(t.name == "head" or t.name == "body" or t.name == "html" or t.name == "br")) {
        // ignore
        _raise();
    }

    // An end tag whose tag name is one of: "head", "body", "html", "br"
    // Anything else
    else {
        auto el = makeStrong<Dom::Element>("html");
        _document->appendChild(el);
        _openElements.pushBack(el);
        _switchTo(Mode::BEFORE_HEAD);
        accept(t);
    }
}

// https://html.spec.whatwg.org/multipage/parsing.html#the-before-head-insertion-mode
void Parser::_handleBeforeHead(Token const &t) {
    // A character token that is one of U+0009 CHARACTER TABULATION,
    // U+000A LINE FEED (LF), U+000C FORM FEED (FF),
    // U+000D CARRIAGE RETURN (CR), or U+0020 SPACE
    if (t.type == Token::CHARACTER and
        (t.rune == '\t' or
         t.rune == '\n' or
         t.rune == '\f' or
         t.rune == ' ')) {
        // Ignore the token.
    }

    // A comment token
    else if (t.type == Token::COMMENT) {
        // Insert a comment.
        insertAComment(*this, t);
    }

    // A comment token
    else if (t.type == Token::DOCTYPE) {
        // Parse error. Ignore the token.
        _raise();
    }

    // A start tag whose tag name is "html"
    else if (t.type == Token::START_TAG and t.name == "html") {
        // Process the token using the rules for the "in body" insertion mode.
        _acceptIn(Mode::IN_BODY, t);
    }

    // A start tag whose tag name is "head"
    else if (t.type == Token::START_TAG and t.name == "head") {
        _headElement = insertHtmlElement(*this, t);
    }

    // Anything else
    else if (t.type == Token::END_TAG and not(t.name == "head" or t.name == "body" or t.name == "html" or t.name == "br")) {
        // ignore
        _raise();
    }

    // An end tag whose tag name is one of: "head", "body", "html", "br"
    // Anything else
    else {
        Token headToken;
        headToken.type = Token::START_TAG;
        headToken.name = String{"head"};
        _headElement = insertHtmlElement(*this, headToken);
        _switchTo(Mode::IN_HEAD);
        accept(t);
    }
}

// https://html.spec.whatwg.org/multipage/parsing.html#parsing-main-inhead
void Parser::_handleInHead(Token const &t) {
    // A character token that is one of U+0009 CHARACTER TABULATION,
    // U+000A LINE FEED (LF), U+000C FORM FEED (FF),
    // U+000D CARRIAGE RETURN (CR), or U+0020 SPACE
    if (t.type == Token::CHARACTER and
        (t.rune == '\t' or
         t.rune == '\n' or
         t.rune == '\f' or
         t.rune == ' ')) {
        insertACharacter(*this, t);
    }

    // A comment token
    else if (t.type == Token::COMMENT) {
        insertAComment(*this, t);
    }

    // A DOCTYPE token
    else if (t.type == Token::DOCTYPE) {
        _raise();
    }

    // A start tag whose tag name is "html"
    else if (t.type == Token::START_TAG and (t.name == "html")) {
        _acceptIn(Mode::IN_BODY, t);
    }

    // A start tag whose tag name is one of: "base", "basefont", "bgsound", "link"
    else if (t.type == Token::START_TAG and (t.name == "base" or t.name == "basefont" or t.name == "bgsound" or t.name == "link")) {
        insertHtmlElement(*this, t);
        _openElements.popBack();
        // TODO: Acknowledge the token's self-closing flag, if it is set.
    }

    // A start tag whose tag name is "meta"
    else if (t.type == Token::START_TAG and (t.name == "meta")) {
        insertHtmlElement(*this, t);
        _openElements.popBack();
        // TODO: Acknowledge the token's self-closing flag, if it is set.

        // TODO: Handle handle speculative parsing
    }

    // A start tag whose tag name is "title"
    else if (t.type == Token::START_TAG and (t.name == "title")) {
        parseRcDataElement(*this, t);
    }

    // A start tag whose tag name is "noscript", if the scripting flag is enabled
    // A start tag whose tag name is one of: "noframes", "style"
    else if (t.type == Token::START_TAG and ((t.name == "noscript" and _scriptingEnabled) or t.name == "noframe" or t.name == "style")) {
        parseRawTextElement(*this, t);
    }

    // A start tag whose tag name is "noscript", if the scripting flag is disabled
    else if (t.type == Token::START_TAG and (t.name == "noscript" and not _scriptingEnabled)) {
        insertHtmlElement(*this, t);
        _switchTo(Mode::IN_HEAD_NOSCRIPT);
    }

    // A start tag whose tag name is "script"
    else if (t.type == Token::START_TAG and (t.name == "script")) {
        // 1. Let the adjusted insertion location be the appropriate place for inserting a node.
        auto localtion = apropriatePlaceForInsertingANode(*this);

        // 2. Create an element for the token in the HTML namespace, with the intended parent being the element in which the adjusted insertion location finds itself.
        auto el = createElementFor(t, Namespace::HTML);

        // 3. Set the element's parser document to the Document, and set the element's force async to false.
        // NOSPEC: We don't support async scripts

        // NOTE: This ensures that, if the script is external, any document.write() calls in the script will execute in-line, instead of blowing the document away, as would happen in most other cases. It also prevents the script from executing until the end tag is seen.

        // 4. If the parser was created as part of the HTML fragment parsing algorithm, then set the script element's already started to true. (fragment case)
        // NOSPEC: We don't support fragments

        // 5. If the parser was invoked via the document.write() or document.writeln() methods, then optionally set the script element's already started to true. (For example, the user agent might use this clause to prevent execution of cross-origin scripts inserted via document.write() under slow network conditions, or when the page has already taken a long time to load.)
        // NOSPEC: We don't support document.write()

        // 6. Insert the newly created element at the adjusted insertion location.
        localtion.insert(el);

        // 7. Push the element onto the stack of open elements so that it is the new current node.
        _openElements.pushBack(el);

        // 8. Switch the tokenizer to the script data state.
        _lexer._switchTo(Lexer::SCRIPT_DATA);

        // 9. Let the original insertion mode be the current insertion mode.
        _originalInsertionMode = _insertionMode;

        // 10. Switch the insertion mode to "text".
        _switchTo(Mode::TEXT);
    }
}

void Parser::_switchTo(Mode mode) {
    _insertionMode = mode;
}

void Parser::_acceptIn(Mode mode, Token const &t) {
    logDebug("Parsing {} in {}", t, toStr(mode));

    switch (mode) {

    case Mode::INITIAL:
        _handleInitialMode(t);
        break;

    case Mode::BEFORE_HTML:
        _handleBeforeHtml(t);
        break;

    case Mode::BEFORE_HEAD:
        _handleBeforeHead(t);
        break;

    case Mode::IN_HEAD:
        _handleInHead(t);
        break;

    // https://html.spec.whatwg.org/multipage/parsing.html#parsing-main-inheadnoscript
    case Mode::IN_HEAD_NOSCRIPT: {
        break;
    }

    // https://html.spec.whatwg.org/multipage/parsing.html#the-after-head-insertion-mode
    case Mode::AFTER_HEAD: {
        break;
    }

    // https://html.spec.whatwg.org/multipage/parsing.html#parsing-main-inbody
    case Mode::IN_BODY: {
        break;
    }

    // https://html.spec.whatwg.org/multipage/parsing.html#parsing-main-intext
    case Mode::TEXT: {
        break;
    }

    // https://html.spec.whatwg.org/multipage/parsing.html#parsing-main-intable
    case Mode::IN_TABLE: {
        break;
    }

    // https://html.spec.whatwg.org/multipage/parsing.html#parsing-main-intabletext
    case Mode::IN_TABLE_TEXT: {
        break;
    }

    // https://html.spec.whatwg.org/multipage/parsing.html#parsing-main-incaption
    case Mode::IN_CAPTION: {
        break;
    }

    // https://html.spec.whatwg.org/multipage/parsing.html#parsing-main-incolumngroup
    case Mode::IN_COLUMN_GROUP: {
        break;
    }

    // https://html.spec.whatwg.org/multipage/parsing.html#parsing-main-intablebody
    case Mode::IN_TABLE_BODY: {
        break;
    }

    // https://html.spec.whatwg.org/multipage/parsing.html#parsing-main-inrow
    case Mode::IN_ROW: {
        break;
    }

    // https://html.spec.whatwg.org/multipage/parsing.html#parsing-main-incell
    case Mode::IN_CELL: {
        break;
    }

    // https://html.spec.whatwg.org/multipage/parsing.html#parsing-main-inselect
    case Mode::IN_SELECT: {
        break;
    }

    // https://html.spec.whatwg.org/multipage/parsing.html#parsing-main-inselectintable
    case Mode::IN_SELECT_IN_TABLE: {
        break;
    }

    // https://html.spec.whatwg.org/multipage/parsing.html#parsing-main-intemplate
    case Mode::IN_TEMPLATE: {
        break;
    }

    // https://html.spec.whatwg.org/multipage/parsing.html#the-after-body-insertion-mode
    case Mode::AFTER_BODY: {
        break;
    }

    // https://html.spec.whatwg.org/multipage/parsing.html#the-in-frameset-insertion-mode
    case Mode::IN_FRAMESET: {
        break;
    }

    // https://html.spec.whatwg.org/multipage/parsing.html#the-after-frameset-insertion-mode
    case Mode::AFTER_FRAMESET: {
        break;
    }

    // https://html.spec.whatwg.org/multipage/parsing.html#the-after-after-body-insertion-mode
    case Mode::AFTER_AFTER_BODY: {
        break;
    }

    // https://html.spec.whatwg.org/multipage/parsing.html#the-after-after-frameset-insertion-mode
    case Mode::AFTER_AFTER_FRAMESET: {
        break;
    }

    default:
        break;
    }
}

void Parser::accept(Token const &t) {
    _acceptIn(_insertionMode, t);
}

} // namespace Web::Html
