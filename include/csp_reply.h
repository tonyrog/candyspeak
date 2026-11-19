// What /dump and /patch say when they refuse: `ERR <code>', and for a few a
// number after it. The words are the HOST's -- utils/Makefile turns the
// comments below into utils/csp_reply.hrl, and csp_cmd puts them back in
// every answer -- so a node carries the numbers and no text. These two
// commands are the protocol a host tool speaks, not a prompt anyone reads.
//
// One per line, `RE_NAME = N,  // text': the generator reads exactly that.
// A number once given keeps its meaning; a new reply takes the next one.
#ifndef __CSP_REPLY_H__
#define __CSP_REPLY_H__

enum {
    RE_USAGE = 1,          // usage: /dump rom|eeprom|patch|info [offs [len]] | rom <slot> | state
    RE_NO_REGION = 2,      // no such region
    RE_NO_IMAGE = 3,       // no image there
    RE_NO_FLASH = 4,       // no flash on this build
    RE_NOTHING_STORED = 5, // nothing stored
    RE_NOT_STARTED = 6,    // not started -- /resume to allocate and run
    RE_NOT_PATCH = 7,      // not a patch
    RE_FORMAT = 8,         // other float format or byte order
    RE_OTHER_IMAGE = 9,    // made for another image
    RE_OTHER_PATCH = 10,   // made for another patch -- the program changed
    RE_FULL = 11,          // full -- nothing changed
    RE_LINE = 12,          // line not understood, at
    RE_OFFS = 13,          // line out of order, at
    RE_CRC = 14,           // line crc wrong, at
    RE_LONG = 15,          // more than the head said -- nothing changed
    RE_BAD_CHANGE = 16,    // a change names a record that is not there -- nothing changed
    RE_LAYOUT = 17,        // it does not fit when laid out -- nothing changed; runtime error
    RE_ABORTED = 18,       // aborted -- nothing changed
    RE_FAILED = 19,        // refused earlier -- nothing changed
    RE_BAD_END = 20,       // size or crc of the whole wrong -- nothing changed
    RE_INCOMPLETE = 21,    // incomplete -- nothing changed
    RE_NOT_RECEIVING = 22, // not receiving
    RE_DIS_DROPPED = 23,   // its #disable set is for another rule count -- dropped
    RE_NO_KEY = 24,        // no such value -- nothing changed, at
    RE_SAVE = 25,          // could not save
    RE_UNKNOWN = 26,       // no such command
    RE_NO_COMPILER = 27,   // no compiler here -- patch from the host
    RE_KIND = 28           // a value of the wrong kind for it -- nothing changed, at
};

// How a value travels by name -- /patch's "CSPN" records, /dump get's answer:
// a kind byte, then 4 bytes little endian, or for a string its length and
// characters. A float goes as IEEE bits; the node turns them into its own.
#define CSP_NV_INT      0
#define CSP_NV_UNSIGNED 1
#define CSP_NV_FLOAT    2
#define CSP_NV_STRING   3
#define CSP_NV_NONE     0xFF   // /dump get: no such value
#define CSP_NV_NOT_ONE  0xFE   // /dump get: not one value (a whole array, say)

#endif
