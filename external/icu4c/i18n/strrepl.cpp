
#include "unicode/utypes.h"

#if !UCONFIG_NO_TRANSLITERATION

#include "strrepl.h"
#include "rbt_data.h"
#include "util.h"
#include "unicode/uniset.h"

U_NAMESPACE_BEGIN

static const UChar EMPTY[] = { 0 }; // empty string: ""

UnicodeReplacer::~UnicodeReplacer() {}
UOBJECT_DEFINE_RTTI_IMPLEMENTATION(StringReplacer)

StringReplacer::StringReplacer(const UnicodeString& theOutput,
                               int32_t theCursorPos,
                               const TransliterationRuleData* theData) {
    output = theOutput;
    cursorPos = theCursorPos;
    hasCursor = TRUE;
    data = theData;
    isComplex = TRUE;
}

StringReplacer::StringReplacer(const UnicodeString& theOutput,
                               const TransliterationRuleData* theData) {
    output = theOutput;
    cursorPos = 0;
    hasCursor = FALSE;
    data = theData;
    isComplex = TRUE;
}

StringReplacer::StringReplacer(const StringReplacer& other) :
    UnicodeFunctor(other),
    UnicodeReplacer(other)
{
    output = other.output;
    cursorPos = other.cursorPos;
    hasCursor = other.hasCursor;
    data = other.data;
    isComplex = other.isComplex;
}

StringReplacer::~StringReplacer() {
}

UnicodeFunctor* StringReplacer::clone() const {
    return new StringReplacer(*this);
}

UnicodeReplacer* StringReplacer::toReplacer() const {
    return (UnicodeReplacer*) this;
}

int32_t StringReplacer::replace(Replaceable& text,
                                int32_t start,
                                int32_t limit,
                                int32_t& cursor) {
    int32_t outLen;
    int32_t newStart = 0;

    // NOTE: It should be possible to _always_ run the complex
    // processing code; just slower.  If not, then there is a bug
    // in the complex processing code.

    // Simple (no nested replacers) Processing Code :
    if (!isComplex) {
        text.handleReplaceBetween(start, limit, output);
        outLen = output.length();

        // Setup default cursor position (for cursorPos within output)
        newStart = cursorPos;
    }

    // Complex (nested replacers) Processing Code :
    else {
        /* When there are segments to be copied, use the Replaceable.copy()
         * API in order to retain out-of-band data.  Copy everything to the
         * end of the string, then copy them back over the key.  This preserves
         * the integrity of indices into the key and surrounding context while
         * generating the output text.
         */
        UnicodeString buf;
        int32_t oOutput; // offset into 'output'
        isComplex = FALSE;

        // The temporary buffer starts at tempStart, and extends
        // to destLimit.  The start of the buffer has a single
        // character from before the key.  This provides style
        // data when addition characters are filled into the
        // temporary buffer.  If there is nothing to the left, use
        // the non-character U+FFFF, which Replaceable subclasses
        // should treat specially as a "no-style character."
        // destStart points to the point after the style context
        // character, so it is tempStart+1 or tempStart+2.
        int32_t tempStart = text.length(); // start of temp buffer
        int32_t destStart = tempStart; // copy new text to here
        if (start > 0) {
            int32_t len = UTF_CHAR_LENGTH(text.char32At(start-1));
            text.copy(start-len, start, tempStart);
            destStart += len;
        } else {
            UnicodeString str((UChar) 0xFFFF);
            text.handleReplaceBetween(tempStart, tempStart, str);
            destStart++;
        }
        int32_t destLimit = destStart;

        for (oOutput=0; oOutput<output.length(); ) {
            if (oOutput == cursorPos) {
                // Record the position of the cursor
                newStart = destLimit - destStart; // relative to start
            }
            UChar32 c = output.char32At(oOutput);
            UnicodeReplacer* r = data->lookupReplacer(c);
            if (r == NULL) {
                // Accumulate straight (non-segment) text.
                buf.append(c);
            } else {
                isComplex = TRUE;

                // Insert any accumulated straight text.
                if (buf.length() > 0) {
                    text.handleReplaceBetween(destLimit, destLimit, buf);
                    destLimit += buf.length();
                    buf.truncate(0);
                }

                // Delegate output generation to replacer object
                int32_t len = r->replace(text, destLimit, destLimit, cursor);
                destLimit += len;
            }
            oOutput += UTF_CHAR_LENGTH(c);
        }
        // Insert any accumulated straight text.
        if (buf.length() > 0) {
            text.handleReplaceBetween(destLimit, destLimit, buf);
            destLimit += buf.length();
        }
        if (oOutput == cursorPos) {
            // Record the position of the cursor
            newStart = destLimit - destStart; // relative to start
        }

        outLen = destLimit - destStart;

        // Copy new text to start, and delete it
        text.copy(destStart, destLimit, start);
        text.handleReplaceBetween(tempStart + outLen, destLimit + outLen, EMPTY);

        // Delete the old text (the key)
        text.handleReplaceBetween(start + outLen, limit + outLen, EMPTY);
    }        

    if (hasCursor) {
        // Adjust the cursor for positions outside the key.  These
        // refer to code points rather than code units.  If cursorPos
        // is within the output string, then use newStart, which has
        // already been set above.
        if (cursorPos < 0) {
            newStart = start;
            int32_t n = cursorPos;
            // Outside the output string, cursorPos counts code points
            while (n < 0 && newStart > 0) {
                newStart -= UTF_CHAR_LENGTH(text.char32At(newStart-1));
                ++n;
            }
            newStart += n;
        } else if (cursorPos > output.length()) {
            newStart = start + outLen;
            int32_t n = cursorPos - output.length();
            // Outside the output string, cursorPos counts code points
            while (n > 0 && newStart < text.length()) {
                newStart += UTF_CHAR_LENGTH(text.char32At(newStart));
                --n;
            }
            newStart += n;
        } else {
            // Cursor is within output string.  It has been set up above
            // to be relative to start.
            newStart += start;
        }

        cursor = newStart;
    }

    return outLen;
}

UnicodeString& StringReplacer::toReplacerPattern(UnicodeString& rule,
                                                 UBool escapeUnprintable) const {
    rule.truncate(0);
    UnicodeString quoteBuf;

    int32_t cursor = cursorPos;

    // Handle a cursor preceding the output
    if (hasCursor && cursor < 0) {
        while (cursor++ < 0) {
            ICU_Utility::appendToRule(rule, (UChar)0x0040 /*@*/, TRUE, escapeUnprintable, quoteBuf);
        }
        // Fall through and append '|' below
    }

    for (int32_t i=0; i<output.length(); ++i) {
        if (hasCursor && i == cursor) {
            ICU_Utility::appendToRule(rule, (UChar)0x007C /*|*/, TRUE, escapeUnprintable, quoteBuf);
        }
        UChar c = output.charAt(i); // Ok to use 16-bits here

        UnicodeReplacer* r = data->lookupReplacer(c);
        if (r == NULL) {
            ICU_Utility::appendToRule(rule, c, FALSE, escapeUnprintable, quoteBuf);
        } else {
            UnicodeString buf;
            r->toReplacerPattern(buf, escapeUnprintable);
            buf.insert(0, (UChar)0x20);
            buf.append((UChar)0x20);
            ICU_Utility::appendToRule(rule, buf,
                                      TRUE, escapeUnprintable, quoteBuf);
        }
    }

    // Handle a cursor after the output.  Use > rather than >= because
    // if cursor == output.length() it is at the end of the output,
    // which is the default position, so we need not emit it.
    if (hasCursor && cursor > output.length()) {
        cursor -= output.length();
        while (cursor-- > 0) {
            ICU_Utility::appendToRule(rule, (UChar)0x0040 /*@*/, TRUE, escapeUnprintable, quoteBuf);
        }
        ICU_Utility::appendToRule(rule, (UChar)0x007C /*|*/, TRUE, escapeUnprintable, quoteBuf);
    }
    // Flush quoteBuf out to result
    ICU_Utility::appendToRule(rule, -1,
                              TRUE, escapeUnprintable, quoteBuf);

    return rule;
}

void StringReplacer::addReplacementSetTo(UnicodeSet& toUnionTo) const {
    UChar32 ch;
    for (int32_t i=0; i<output.length(); i+=UTF_CHAR_LENGTH(ch)) {
    ch = output.char32At(i);
    UnicodeReplacer* r = data->lookupReplacer(ch);
    if (r == NULL) {
        toUnionTo.add(ch);
    } else {
        r->addReplacementSetTo(toUnionTo);
    }
    }
}

void StringReplacer::setData(const TransliterationRuleData* d) {
    data = d;
    int32_t i = 0;
    while (i<output.length()) {
        UChar32 c = output.char32At(i);
        UnicodeFunctor* f = data->lookup(c);
        if (f != NULL) {
            f->setData(data);
        }
        i += UTF_CHAR_LENGTH(c);
    }
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_TRANSLITERATION */

//eof
