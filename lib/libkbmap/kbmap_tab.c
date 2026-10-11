/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	kbmap_tab.c
 *	The layouts the system carries (design 11.14)
 *
 *	The names of the keys, the keyboard and the kana layouts an object
 *	starts with or may choose besides its own, and the spellings of
 *	romaji. The layouts are kept as the JSON an object keeps its own
 *	in, read by the same calls, so that a layout of the system and one
 *	brought in are one and the same thing.
 */

#include <ts/kbmap.h>

/* ---------------------------------------------------------------- the keys' names */

typedef struct {
	UH		usage;
	CONST char	*name;
} KBNAME;

/*
 * KeyboardEvent.code for each usage. The Japanese keyboard's ] key is
 * reported as 0x32 by most keyboards and 0x31 by some; both are
 * "Backslash", the first being the one a name is turned into.
 */
LOCAL CONST KBNAME kb_names[] = {
	{ 0x04, "KeyA" }, { 0x05, "KeyB" }, { 0x06, "KeyC" }, { 0x07, "KeyD" },
	{ 0x08, "KeyE" }, { 0x09, "KeyF" }, { 0x0A, "KeyG" }, { 0x0B, "KeyH" },
	{ 0x0C, "KeyI" }, { 0x0D, "KeyJ" }, { 0x0E, "KeyK" }, { 0x0F, "KeyL" },
	{ 0x10, "KeyM" }, { 0x11, "KeyN" }, { 0x12, "KeyO" }, { 0x13, "KeyP" },
	{ 0x14, "KeyQ" }, { 0x15, "KeyR" }, { 0x16, "KeyS" }, { 0x17, "KeyT" },
	{ 0x18, "KeyU" }, { 0x19, "KeyV" }, { 0x1A, "KeyW" }, { 0x1B, "KeyX" },
	{ 0x1C, "KeyY" }, { 0x1D, "KeyZ" },
	{ 0x1E, "Digit1" }, { 0x1F, "Digit2" }, { 0x20, "Digit3" }, { 0x21, "Digit4" },
	{ 0x22, "Digit5" }, { 0x23, "Digit6" }, { 0x24, "Digit7" }, { 0x25, "Digit8" },
	{ 0x26, "Digit9" }, { 0x27, "Digit0" },
	{ 0x28, "Enter" }, { 0x29, "Escape" }, { 0x2A, "Backspace" }, { 0x2B, "Tab" },
	{ 0x2C, "Space" }, { 0x2D, "Minus" }, { 0x2E, "Equal" }, { 0x2F, "BracketLeft" },
	{ 0x30, "BracketRight" }, { 0x32, "Backslash" }, { 0x31, "Backslash" },
	{ 0x33, "Semicolon" }, { 0x34, "Quote" }, { 0x35, "Backquote" }, { 0x36, "Comma" },
	{ 0x37, "Period" }, { 0x38, "Slash" }, { 0x39, "CapsLock" },
	{ 0x3A, "F1" }, { 0x3B, "F2" }, { 0x3C, "F3" }, { 0x3D, "F4" }, { 0x3E, "F5" },
	{ 0x3F, "F6" }, { 0x40, "F7" }, { 0x41, "F8" }, { 0x42, "F9" }, { 0x43, "F10" },
	{ 0x44, "F11" }, { 0x45, "F12" },
	{ 0x46, "PrintScreen" }, { 0x47, "ScrollLock" }, { 0x48, "Pause" }, { 0x49, "Insert" },
	{ 0x4A, "Home" }, { 0x4B, "PageUp" }, { 0x4C, "Delete" }, { 0x4D, "End" },
	{ 0x4E, "PageDown" }, { 0x4F, "ArrowRight" }, { 0x50, "ArrowLeft" }, { 0x51, "ArrowDown" },
	{ 0x52, "ArrowUp" }, { 0x53, "NumLock" }, { 0x65, "ContextMenu" },
	{ 0x87, "IntlRo" }, { 0x88, "KanaMode" }, { 0x89, "IntlYen" }, { 0x8A, "Convert" },
	{ 0x8B, "NonConvert" }, { 0x90, "Lang1" }, { 0x91, "Lang2" },
	{ 0xE0, "ControlLeft" }, { 0xE1, "ShiftLeft" }, { 0xE2, "AltLeft" }, { 0xE3, "MetaLeft" },
	{ 0xE4, "ControlRight" }, { 0xE5, "ShiftRight" }, { 0xE6, "AltRight" }, { 0xE7, "MetaRight" },
};

EXPORT UINT kb_usage( CONST UB *name, INT len )
{
	UINT	i;
	INT	k;

	for ( i = 0; i < sizeof(kb_names) / sizeof(kb_names[0]); i++ ) {
		CONST char	*n = kb_names[i].name;

		for ( k = 0; k < len && n[k] != 0 && (UB)n[k] == name[k]; k++ ) ;
		if ( k == len && n[k] == 0 ) {
			return kb_names[i].usage;
		}
	}
	return 0;
}

EXPORT CONST char *kb_key_name( UINT usage )
{
	UINT	i;

	for ( i = 0; i < sizeof(kb_names) / sizeof(kb_names[0]); i++ ) {
		if ( kb_names[i].usage == usage ) {
			return kb_names[i].name;
		}
	}
	return NULL;
}

/* ---------------------------------------------------------------- romaji */

/*
 * The ways each kana is spelt in romaji, the Hepburn one first. っ is
 * spelt besides by doubling the letter the next kana starts with, and
 * ん by a single n before anything but a vowel, y or n; those two the
 * reader works out, not this table.
 */
#define R1(k, a)		{ { k, 0, 0 }, { a } }
#define R2(k, a, b)		{ { k, 0, 0 }, { a, b } }
#define R3(k, a, b, c)		{ { k, 0, 0 }, { a, b, c } }
#define R4(k, a, b, c, d)	{ { k, 0, 0 }, { a, b, c, d } }
#define Y1(k, s, a)		{ { k, s, 0 }, { a } }
#define Y2(k, s, a, b)		{ { k, s, 0 }, { a, b } }
#define Y3(k, s, a, b, c)	{ { k, s, 0 }, { a, b, c } }

EXPORT CONST KBROMA kb_roma[] = {
	/* two kana spelt as one: tried before the first of them alone */
	Y2(0x304D, 0x3083, "kya", "kixya"), Y2(0x304D, 0x3085, "kyu", "kixyu"), Y2(0x304D, 0x3087, "kyo", "kixyo"),
	Y2(0x304D, 0x3047, "kye", "kixe"),
	Y2(0x304E, 0x3083, "gya", "gixya"), Y2(0x304E, 0x3085, "gyu", "gixyu"), Y2(0x304E, 0x3087, "gyo", "gixyo"),
	Y2(0x304E, 0x3047, "gye", "gixe"),
	Y3(0x3057, 0x3083, "sha", "sya", "shixya"), Y3(0x3057, 0x3085, "shu", "syu", "shixyu"),
	Y3(0x3057, 0x3087, "sho", "syo", "shixyo"), Y3(0x3057, 0x3047, "she", "sye", "shixe"),
	Y3(0x3058, 0x3083, "ja", "jya", "zya"), Y3(0x3058, 0x3085, "ju", "jyu", "zyu"),
	Y3(0x3058, 0x3087, "jo", "jyo", "zyo"), Y3(0x3058, 0x3047, "je", "jye", "zye"),
	Y3(0x3061, 0x3083, "cha", "tya", "cya"), Y3(0x3061, 0x3085, "chu", "tyu", "cyu"),
	Y3(0x3061, 0x3087, "cho", "tyo", "cyo"), Y3(0x3061, 0x3047, "che", "tye", "cye"),
	Y1(0x3062, 0x3083, "dya"), Y1(0x3062, 0x3085, "dyu"), Y1(0x3062, 0x3087, "dyo"),
	Y1(0x306B, 0x3083, "nya"), Y1(0x306B, 0x3085, "nyu"), Y1(0x306B, 0x3087, "nyo"),
	Y2(0x3072, 0x3083, "hya", "hixya"), Y2(0x3072, 0x3085, "hyu", "hixyu"), Y2(0x3072, 0x3087, "hyo", "hixyo"),
	Y1(0x3073, 0x3083, "bya"), Y1(0x3073, 0x3085, "byu"), Y1(0x3073, 0x3087, "byo"),
	Y1(0x3074, 0x3083, "pya"), Y1(0x3074, 0x3085, "pyu"), Y1(0x3074, 0x3087, "pyo"),
	Y1(0x307F, 0x3083, "mya"), Y1(0x307F, 0x3085, "myu"), Y1(0x307F, 0x3087, "myo"),
	Y1(0x308A, 0x3083, "rya"), Y1(0x308A, 0x3085, "ryu"), Y1(0x308A, 0x3087, "ryo"),
	Y2(0x3075, 0x3041, "fa", "fuxa"), Y2(0x3075, 0x3043, "fi", "fuxi"), Y2(0x3075, 0x3047, "fe", "fuxe"),
	Y2(0x3075, 0x3049, "fo", "fuxo"),
	Y2(0x3066, 0x3043, "thi", "texi"), Y2(0x3067, 0x3043, "dhi", "dexi"),
	Y2(0x3068, 0x3045, "twu", "toxu"), Y2(0x3069, 0x3045, "dwu", "doxu"),
	Y2(0x3046, 0x3043, "wi", "uxi"), Y2(0x3046, 0x3047, "we", "uxe"), Y2(0x3046, 0x3049, "wo", "uxo"),
	Y2(0x3044, 0x3047, "ye", "ixe"),
	Y1(0x3094, 0x3041, "va"), Y1(0x3094, 0x3043, "vi"), Y1(0x3094, 0x3047, "ve"), Y1(0x3094, 0x3049, "vo"),
	/* one kana */
	R1(0x3042, "a"), R1(0x3044, "i"), R2(0x3046, "u", "wu"), R1(0x3048, "e"), R1(0x304A, "o"),
	R2(0x304B, "ka", "ca"), R1(0x304D, "ki"), R3(0x304F, "ku", "cu", "qu"), R1(0x3051, "ke"),
	R2(0x3053, "ko", "co"),
	R1(0x304C, "ga"), R1(0x304E, "gi"), R1(0x3050, "gu"), R1(0x3052, "ge"), R1(0x3054, "go"),
	R1(0x3055, "sa"), R3(0x3057, "shi", "si", "ci"), R1(0x3059, "su"), R2(0x305B, "se", "ce"),
	R1(0x305D, "so"),
	R1(0x3056, "za"), R2(0x3058, "ji", "zi"), R1(0x305A, "zu"), R1(0x305C, "ze"), R1(0x305E, "zo"),
	R1(0x305F, "ta"), R2(0x3061, "chi", "ti"), R2(0x3064, "tsu", "tu"), R1(0x3066, "te"),
	R1(0x3068, "to"),
	R1(0x3060, "da"), R1(0x3062, "di"), R1(0x3065, "du"), R1(0x3067, "de"), R1(0x3069, "do"),
	R1(0x306A, "na"), R1(0x306B, "ni"), R1(0x306C, "nu"), R1(0x306D, "ne"), R1(0x306E, "no"),
	R1(0x306F, "ha"), R1(0x3072, "hi"), R2(0x3075, "fu", "hu"), R1(0x3078, "he"), R1(0x307B, "ho"),
	R1(0x3070, "ba"), R1(0x3073, "bi"), R1(0x3076, "bu"), R1(0x3079, "be"), R1(0x307C, "bo"),
	R1(0x3071, "pa"), R1(0x3074, "pi"), R1(0x3077, "pu"), R1(0x307A, "pe"), R1(0x307D, "po"),
	R1(0x307E, "ma"), R1(0x307F, "mi"), R1(0x3080, "mu"), R1(0x3081, "me"), R1(0x3082, "mo"),
	R1(0x3084, "ya"), R1(0x3086, "yu"), R1(0x3088, "yo"),
	R1(0x3089, "ra"), R1(0x308A, "ri"), R1(0x308B, "ru"), R1(0x308C, "re"), R1(0x308D, "ro"),
	R1(0x308F, "wa"), R1(0x3090, "wyi"), R1(0x3091, "wye"), R1(0x3092, "wo"),
	R2(0x3093, "nn", "xn"), R1(0x3094, "vu"),
	R2(0x3041, "xa", "la"), R2(0x3043, "xi", "li"), R2(0x3045, "xu", "lu"), R2(0x3047, "xe", "le"),
	R2(0x3049, "xo", "lo"), R2(0x3083, "xya", "lya"), R2(0x3085, "xyu", "lyu"), R2(0x3087, "xyo", "lyo"),
	R4(0x3063, "xtu", "ltu", "xtsu", "ltsu"), R2(0x308E, "xwa", "lwa"), R1(0x3095, "xka"),
	R1(0x3096, "xke"),
	R1(0x30FC, "-"), R1(0x3001, ","), R1(0x3002, "."), R1(0x30FB, "/"),
};

EXPORT CONST INT kb_nroma = (INT)( sizeof(kb_roma) / sizeof(kb_roma[0]) );

/* The keys romaji is typed with: the letters, and the signs that are kana */
EXPORT UINT kb_roma_key( char c )
{
	if ( c >= 'a' && c <= 'z' ) return 0x04 + (UINT)( c - 'a' );
	switch ( c ) {
	case '-':	return 0x2D;
	case ',':	return 0x36;
	case '.':	return 0x37;
	case '/':	return 0x38;
	default:	return 0;
	}
}

EXPORT char kb_roma_char( UINT usage )
{
	if ( usage >= 0x04 && usage <= 0x1D ) return (char)( 'a' + ( usage - 0x04 ) );
	switch ( usage ) {
	case 0x2D:	return '-';
	case 0x36:	return ',';
	case 0x37:	return '.';
	case 0x38:	return '/';
	default:	return 0;
	}
}

/* ---------------------------------------------------------------- the layouts, as JSON */

EXPORT CONST char kb_jp109_json[] =
	"{\"meta\":{\"name\":\"日本語109キー\",\"version\":\"1.0\",\"format\":\"kle-extended\"},\"layout\":[[{\"id\":\"Escape\"},\""
	"Esc\",{\"x\":0.5,\"id\":\"F1\"},\"F1\",{\"id\":\"F2\"},\"F2\",{\"id\":\"F3\"},\"F3\",{\"id\":\"F4\"},\"F4\",{\"x\":0.5,\"id\":\""
	"F5\"},\"F5\",{\"id\":\"F6\"},\"F6\",{\"id\":\"F7\"},\"F7\",{\"id\":\"F8\"},\"F8\",{\"x\":0.5,\"id\":\"F9\"},\"F9\",{\"id\":\"F10"
	"\"},\"F10\",{\"id\":\"F11\"},\"F11\",{\"id\":\"F12\"},\"F12\"],[{\"y\":0.5,\"id\":\"Backquote\"},\"半/全\",{\"id\":\"Digit1\""
	"},\"!\\n1\",{\"id\":\"Digit2\"},\"\\\"\\n2\",{\"id\":\"Digit3\"},\"#\\n3\",{\"id\":\"Digit4\"},\"$\\n4\",{\"id\":\"Digit5\"},\""
	"%\\n5\",{\"id\":\"Digit6\"},\"&\\n6\",{\"id\":\"Digit7\"},\"'\\n7\",{\"id\":\"Digit8\"},\"(\\n8\",{\"id\":\"Digit9\"},\")\\n9"
	"\",{\"id\":\"Digit0\"},\"\\n0\",{\"id\":\"Minus\"},\"=\\n-\",{\"id\":\"Equal\"},\"~\\n^\",{\"id\":\"IntlYen\"},\"|\\n¥\",{\"id"
	"\":\"Backspace\"},\"BS\"],[{\"id\":\"Tab\",\"w\":1.5},\"Tab\",{\"id\":\"KeyQ\"},\"Q\",{\"id\":\"KeyW\"},\"W\",{\"id\":\"KeyE"
	"\"},\"E\",{\"id\":\"KeyR\"},\"R\",{\"id\":\"KeyT\"},\"T\",{\"id\":\"KeyY\"},\"Y\",{\"id\":\"KeyU\"},\"U\",{\"id\":\"KeyI\"},\"I\""
	",{\"id\":\"KeyO\"},\"O\",{\"id\":\"KeyP\"},\"P\",{\"id\":\"BracketLeft\"},\"@\",{\"id\":\"BracketRight\"},\"[\",{\"id\":\"E"
	"nter\",\"x\":0.25,\"w\":1.25,\"h\":2},\"Enter\"],[{\"id\":\"CapsLock\",\"w\":1.75},\"英数\",{\"id\":\"KeyA\"},\"A\",{\"id\""
	":\"KeyS\"},\"S\",{\"id\":\"KeyD\"},\"D\",{\"id\":\"KeyF\"},\"F\",{\"id\":\"KeyG\"},\"G\",{\"id\":\"KeyH\"},\"H\",{\"id\":\"KeyJ"
	"\"},\"J\",{\"id\":\"KeyK\"},\"K\",{\"id\":\"KeyL\"},\"L\",{\"id\":\"Semicolon\"},\";\",{\"id\":\"Quote\"},\":\",{\"id\":\"Back"
	"slash\"},\"]\"],[{\"id\":\"ShiftLeft\",\"w\":2.25},\"Shift\",{\"id\":\"KeyZ\"},\"Z\",{\"id\":\"KeyX\"},\"X\",{\"id\":\"Key"
	"C\"},\"C\",{\"id\":\"KeyV\"},\"V\",{\"id\":\"KeyB\"},\"B\",{\"id\":\"KeyN\"},\"N\",{\"id\":\"KeyM\"},\"M\",{\"id\":\"Comma\"},\""
	",\",{\"id\":\"Period\"},\".\",{\"id\":\"Slash\"},\"/\",{\"id\":\"IntlRo\"},\"\\\\\",{\"id\":\"ShiftRight\",\"w\":1.75},\"Shi"
	"ft\"],[{\"id\":\"ControlLeft\",\"w\":1.5},\"Ctrl\",{\"id\":\"MetaLeft\"},\"Win\",{\"id\":\"AltLeft\",\"w\":1.25},\"Alt"
	"\",{\"id\":\"NonConvert\",\"w\":1.25},\"無変換\",{\"id\":\"Space\",\"w\":4},\"\",{\"id\":\"Convert\",\"w\":1.25},\"変換\",{\"id"
	"\":\"KanaMode\",\"w\":1.25},\"かな\",{\"id\":\"AltRight\"},\"Alt\",{\"id\":\"ControlRight\",\"w\":1.5},\"Ctrl\"]]}";

EXPORT CONST char kb_tron_json[] =
	"{\"meta\":{\"name\":\"TRONかな配列 (フォールバック)\",\"version\":\"1.0\"},\"mapping\":{\"KeyQ\":{\"normal\":\"ら\",\"leftShift\":\"ひ\",\"rightSh"
	"ift\":\"び\"},\"KeyW\":{\"normal\":\"る\",\"leftShift\":\"そ\",\"rightShift\":\"ぞ\"},\"KeyE\":{\"normal\":\"こ\",\"leftShift\":\"・\",\"rightSh"
	"ift\":\"ご\"},\"KeyR\":{\"normal\":\"は\",\"leftShift\":\"ゃ\",\"rightShift\":\"ば\"},\"KeyT\":{\"normal\":\"ょ\",\"leftShift\":\"ほ\",\"rightSh"
	"ift\":\"ぼ\"},\"KeyY\":{\"normal\":\"き\",\"leftShift\":\"ぎ\",\"rightShift\":\"え\"},\"KeyU\":{\"normal\":\"の\",\"leftShift\":\"げ\",\"rightSh"
	"ift\":\"け\"},\"KeyI\":{\"normal\":\"く\",\"leftShift\":\"ぐ\",\"rightShift\":\"め\"},\"KeyO\":{\"normal\":\"あ\",\"leftShift\":\"○\",\"rightSh"
	"ift\":\"む\"},\"KeyP\":{\"normal\":\"れ\",\"leftShift\":\"ゐ\",\"rightShift\":\"ろ\"},\"KeyA\":{\"normal\":\"た\",\"leftShift\":\"ぬ\",\"rightSh"
	"ift\":\"だ\"},\"KeyS\":{\"normal\":\"と\",\"leftShift\":\"ね\",\"rightShift\":\"ど\"},\"KeyD\":{\"normal\":\"か\",\"leftShift\":\"ゅ\",\"rightSh"
	"ift\":\"が\"},\"KeyF\":{\"normal\":\"て\",\"leftShift\":\"よ\",\"rightShift\":\"で\"},\"KeyG\":{\"normal\":\"も\",\"leftShift\":\"ふ\",\"rightSh"
	"ift\":\"ぶ\"},\"KeyH\":{\"normal\":\"を\",\"leftShift\":\"゛\",\"rightShift\":\"お\"},\"KeyJ\":{\"normal\":\"い\",\"leftShift\":\"ぢ\",\"rightSh"
	"ift\":\"ち\"},\"KeyK\":{\"normal\":\"う\",\"leftShift\":\"ヴ\",\"rightShift\":\"ー\"},\"KeyL\":{\"normal\":\"し\",\"leftShift\":\"じ\",\"rightSh"
	"ift\":\"み\"},\"Semicolon\":{\"normal\":\"ん\",\"leftShift\":\"ゑ\",\"rightShift\":\"や\"},\"Quote\":{\"normal\":\"ー\"},\"KeyZ\":{\"normal\":"
	"\"ま\",\"leftShift\":\"ぇ\",\"rightShift\":\"ヵ\"},\"KeyX\":{\"normal\":\"り\",\"leftShift\":\"ぉ\",\"rightShift\":\"ヶ\"},\"KeyC\":{\"normal\":"
	"\"に\",\"leftShift\":\"せ\",\"rightShift\":\"ぜ\"},\"KeyV\":{\"normal\":\"さ\",\"leftShift\":\"ゆ\",\"rightShift\":\"ざ\"},\"KeyB\":{\"normal\":"
	"\"な\",\"leftShift\":\"へ\",\"rightShift\":\"べ\"},\"KeyN\":{\"normal\":\"す\",\"leftShift\":\"ず\",\"rightShift\":\"わ\"},\"KeyM\":{\"normal\":"
	"\"つ\",\"leftShift\":\"づ\",\"rightShift\":\"ぃ\"},\"Comma\":{\"normal\":\"、\",\"leftShift\":\"，\",\"rightShift\":\"ぁ\"},\"Period\":{\"norma"
	"l\":\"。\",\"leftShift\":\"．\",\"rightShift\":\"゜\"},\"Slash\":{\"normal\":\"っ\",\"leftShift\":\"ゎ\",\"rightShift\":\"ぅ\"}},\"handAssignm"
	"ent\":{\"left\":[\"KeyQ\",\"KeyW\",\"KeyE\",\"KeyR\",\"KeyT\",\"KeyA\",\"KeyS\",\"KeyD\",\"KeyF\",\"KeyG\",\"KeyZ\",\"KeyX\",\"KeyC\",\"KeyV"
	"\",\"KeyB\",\"ShiftLeft\"],\"right\":[\"KeyY\",\"KeyU\",\"KeyI\",\"KeyO\",\"KeyP\",\"KeyH\",\"KeyJ\",\"KeyK\",\"KeyL\",\"Semicolon\",\"Quo"
	"te\",\"KeyN\",\"KeyM\",\"Comma\",\"Period\",\"Slash\",\"ShiftRight\",\"Space\"]},\"fingerAssignment\":{\"left_pinky\":[\"KeyQ\",\"Ke"
	"yA\",\"KeyZ\"],\"left_ring\":[\"KeyW\",\"KeyS\",\"KeyX\"],\"left_middle\":[\"KeyE\",\"KeyD\",\"KeyC\"],\"left_index\":[\"KeyR\",\"KeyT"
	"\",\"KeyF\",\"KeyG\",\"KeyV\",\"KeyB\"],\"right_index\":[\"KeyY\",\"KeyU\",\"KeyH\",\"KeyJ\",\"KeyN\",\"KeyM\"],\"right_middle\":[\"KeyI"
	"\",\"KeyK\",\"Comma\"],\"right_ring\":[\"KeyO\",\"KeyL\",\"Period\"],\"right_pinky\":[\"KeyP\",\"Semicolon\",\"Quote\",\"Slash\"]}}";

EXPORT CONST char kb_jis_json[] =
	"{\"meta\":{\"name\":\"JISかな\",\"version\":\"1.0\",\"description\":\"日本語キーボードに刻印されたかな。シフトはどちらでもよい。\"},\"mapping\""
	":{\"Digit1\":{\"normal\":\"ぬ\"},\"Digit2\":{\"normal\":\"ふ\"},\"Digit3\":{\"normal\":\"あ\",\"leftShift\":\"ぁ\",\"rightS"
	"hift\":\"ぁ\"},\"Digit4\":{\"normal\":\"う\",\"leftShift\":\"ぅ\",\"rightShift\":\"ぅ\"},\"Digit5\":{\"normal\":\"え\",\"left"
	"Shift\":\"ぇ\",\"rightShift\":\"ぇ\"},\"Digit6\":{\"normal\":\"お\",\"leftShift\":\"ぉ\",\"rightShift\":\"ぉ\"},\"Digit7\":{"
	"\"normal\":\"や\",\"leftShift\":\"ゃ\",\"rightShift\":\"ゃ\"},\"Digit8\":{\"normal\":\"ゆ\",\"leftShift\":\"ゅ\",\"rightShif"
	"t\":\"ゅ\"},\"Digit9\":{\"normal\":\"よ\",\"leftShift\":\"ょ\",\"rightShift\":\"ょ\"},\"Digit0\":{\"normal\":\"わ\",\"leftShi"
	"ft\":\"を\",\"rightShift\":\"を\"},\"Minus\":{\"normal\":\"ほ\"},\"Equal\":{\"normal\":\"へ\"},\"IntlYen\":{\"normal\":\"ー\"}"
	",\"KeyQ\":{\"normal\":\"た\"},\"KeyW\":{\"normal\":\"て\"},\"KeyE\":{\"normal\":\"い\",\"leftShift\":\"ぃ\",\"rightShift\":\""
	"ぃ\"},\"KeyR\":{\"normal\":\"す\"},\"KeyT\":{\"normal\":\"か\"},\"KeyY\":{\"normal\":\"ん\"},\"KeyU\":{\"normal\":\"な\"},\"Key"
	"I\":{\"normal\":\"に\"},\"KeyO\":{\"normal\":\"ら\"},\"KeyP\":{\"normal\":\"せ\"},\"BracketLeft\":{\"normal\":\"゛\"},\"Brac"
	"ketRight\":{\"normal\":\"゜\",\"leftShift\":\"「\",\"rightShift\":\"「\"},\"KeyA\":{\"normal\":\"ち\"},\"KeyS\":{\"normal\""
	":\"と\"},\"KeyD\":{\"normal\":\"し\"},\"KeyF\":{\"normal\":\"は\"},\"KeyG\":{\"normal\":\"き\"},\"KeyH\":{\"normal\":\"く\"},\"K"
	"eyJ\":{\"normal\":\"ま\"},\"KeyK\":{\"normal\":\"の\"},\"KeyL\":{\"normal\":\"り\"},\"Semicolon\":{\"normal\":\"れ\"},\"Quot"
	"e\":{\"normal\":\"け\"},\"Backslash\":{\"normal\":\"む\",\"leftShift\":\"」\",\"rightShift\":\"」\"},\"KeyZ\":{\"normal\":\""
	"つ\",\"leftShift\":\"っ\",\"rightShift\":\"っ\"},\"KeyX\":{\"normal\":\"さ\"},\"KeyC\":{\"normal\":\"そ\"},\"KeyV\":{\"normal"
	"\":\"ひ\"},\"KeyB\":{\"normal\":\"こ\"},\"KeyN\":{\"normal\":\"み\"},\"KeyM\":{\"normal\":\"も\"},\"Comma\":{\"normal\":\"ね\",\""
	"leftShift\":\"、\",\"rightShift\":\"、\"},\"Period\":{\"normal\":\"る\",\"leftShift\":\"。\",\"rightShift\":\"。\"},\"Slash"
	"\":{\"normal\":\"め\",\"leftShift\":\"・\",\"rightShift\":\"・\"},\"IntlRo\":{\"normal\":\"ろ\"}},\"reverseMapping\":{\"_a"
	"uto\":true},\"handAssignment\":{\"left\":[\"Backquote\",\"Digit1\",\"Digit2\",\"Digit3\",\"Digit4\",\"Digit5\",\"T"
	"ab\",\"KeyQ\",\"KeyW\",\"KeyE\",\"KeyR\",\"KeyT\",\"CapsLock\",\"KeyA\",\"KeyS\",\"KeyD\",\"KeyF\",\"KeyG\",\"ShiftLeft\""
	",\"KeyZ\",\"KeyX\",\"KeyC\",\"KeyV\",\"KeyB\",\"ControlLeft\",\"MetaLeft\",\"AltLeft\",\"NonConvert\"],\"right\":[\"D"
	"igit6\",\"Digit7\",\"Digit8\",\"Digit9\",\"Digit0\",\"Minus\",\"Equal\",\"IntlYen\",\"Backspace\",\"KeyY\",\"KeyU\",\""
	"KeyI\",\"KeyO\",\"KeyP\",\"BracketLeft\",\"BracketRight\",\"Enter\",\"KeyH\",\"KeyJ\",\"KeyK\",\"KeyL\",\"Semicolon\""
	",\"Quote\",\"Backslash\",\"KeyN\",\"KeyM\",\"Comma\",\"Period\",\"Slash\",\"IntlRo\",\"ShiftRight\",\"Space\",\"Conve"
	"rt\",\"KanaMode\",\"AltRight\",\"ControlRight\"]},\"fingerAssignment\":{\"left_pinky\":[\"Backquote\",\"Digit1"
	"\",\"Tab\",\"KeyQ\",\"CapsLock\",\"KeyA\",\"ShiftLeft\",\"KeyZ\",\"ControlLeft\"],\"left_ring\":[\"Digit2\",\"KeyW\","
	"\"KeyS\",\"KeyX\"],\"left_middle\":[\"Digit3\",\"KeyE\",\"KeyD\",\"KeyC\"],\"left_index\":[\"Digit4\",\"Digit5\",\"Ke"
	"yR\",\"KeyT\",\"KeyF\",\"KeyG\",\"KeyV\",\"KeyB\"],\"left_thumb\":[\"MetaLeft\",\"AltLeft\",\"NonConvert\"],\"right_"
	"index\":[\"Digit6\",\"Digit7\",\"KeyY\",\"KeyU\",\"KeyH\",\"KeyJ\",\"KeyN\",\"KeyM\"],\"right_middle\":[\"Digit8\",\"K"
	"eyI\",\"KeyK\",\"Comma\"],\"right_ring\":[\"Digit9\",\"KeyO\",\"KeyL\",\"Period\"],\"right_pinky\":[\"Digit0\",\"Min"
	"us\",\"Equal\",\"IntlYen\",\"Backspace\",\"KeyP\",\"BracketLeft\",\"BracketRight\",\"Enter\",\"Semicolon\",\"Quote"
	"\",\"Backslash\",\"Slash\",\"IntlRo\",\"ShiftRight\"],\"right_thumb\":[\"Space\",\"Convert\",\"KanaMode\",\"AltRig"
	"ht\",\"ControlRight\"]}}";
