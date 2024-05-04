// A Bison parser, made by GNU Bison 3.5.1.

// Skeleton interface for Bison LALR(1) parsers in C++

// Copyright (C) 2002-2015, 2018-2020 Free Software Foundation, Inc.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

// As a special exception, you may create a larger work that contains
// part or all of the Bison parser skeleton and distribute that work
// under terms of your choice, so long as that work isn't itself a
// parser generator using the skeleton or a modified version thereof
// as a parser skeleton.  Alternatively, if you modify or redistribute
// the parser skeleton itself, you may (at your option) remove this
// special exception, which will cause the skeleton and the resulting
// Bison output files to be licensed under the GNU General Public
// License without this special exception.

// This special exception was added by the Free Software Foundation in
// version 2.2 of Bison.


/**
 ** \file /home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/lef/bison/LefParser.h
 ** Define the LefParser::parser class.
 */

// C++ LALR(1) parser skeleton written by Akim Demaille.

// Undocumented macros, especially those whose name start with YY_,
// are private implementation details.  Do not rely on them.

#ifndef YY_LEFPARSER_HOME_MSEARS_AIEPLACE_CPP_LIMBO_BUILD_LIMBO_PARSERS_LEF_BISON_LEFPARSER_H_INCLUDED
# define YY_LEFPARSER_HOME_MSEARS_AIEPLACE_CPP_LIMBO_BUILD_LIMBO_PARSERS_LEF_BISON_LEFPARSER_H_INCLUDED


# include <cstdlib> // std::abort
# include <iostream>
# include <stdexcept>
# include <string>
# include <vector>

#if defined __cplusplus
# define YY_CPLUSPLUS __cplusplus
#else
# define YY_CPLUSPLUS 199711L
#endif

// Support move semantics when possible.
#if 201103L <= YY_CPLUSPLUS
# define YY_MOVE           std::move
# define YY_MOVE_OR_COPY   move
# define YY_MOVE_REF(Type) Type&&
# define YY_RVREF(Type)    Type&&
# define YY_COPY(Type)     Type
#else
# define YY_MOVE
# define YY_MOVE_OR_COPY   copy
# define YY_MOVE_REF(Type) Type&
# define YY_RVREF(Type)    const Type&
# define YY_COPY(Type)     const Type&
#endif

// Support noexcept when possible.
#if 201103L <= YY_CPLUSPLUS
# define YY_NOEXCEPT noexcept
# define YY_NOTHROW
#else
# define YY_NOEXCEPT
# define YY_NOTHROW throw ()
#endif

// Support constexpr when possible.
#if 201703 <= YY_CPLUSPLUS
# define YY_CONSTEXPR constexpr
#else
# define YY_CONSTEXPR
#endif
# include "location.hh"


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(E) ((void) (E))
#else
# define YYUSE(E) /* empty */
#endif

#if defined __GNUC__ && ! defined __ICC && 407 <= __GNUC__ * 100 + __GNUC_MINOR__
/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                            \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

/* Debug traces.  */
#ifndef LEFPARSERDEBUG
# if defined YYDEBUG
#if YYDEBUG
#   define LEFPARSERDEBUG 1
#  else
#   define LEFPARSERDEBUG 0
#  endif
# else /* ! defined YYDEBUG */
#  define LEFPARSERDEBUG 1
# endif /* ! defined YYDEBUG */
#endif  /* ! defined LEFPARSERDEBUG */

namespace LefParser {
#line 183 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/lef/bison/LefParser.h"




  /// A Bison parser.
  class Parser
  {
  public:
#ifndef LEFPARSERSTYPE
    /// Symbol semantic values.
    union semantic_type
    {
#line 62 "LefParser.yy"

    int  			integerVal;
    double 			doubleVal;
	/*char*           string;*/
    std::string*		stringVal;
	std::string*		qstringVal;
	std::string*		binaryVal;

	struct lefPoint* pt; 

#line 207 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/lef/bison/LefParser.h"

    };
#else
    typedef LEFPARSERSTYPE semantic_type;
#endif
    /// Symbol locations.
    typedef location location_type;

    /// Syntax errors thrown from user actions.
    struct syntax_error : std::runtime_error
    {
      syntax_error (const location_type& l, const std::string& m)
        : std::runtime_error (m)
        , location (l)
      {}

      syntax_error (const syntax_error& s)
        : std::runtime_error (s.what ())
        , location (s.location)
      {}

      ~syntax_error () YY_NOEXCEPT YY_NOTHROW;

      location_type location;
    };

    /// Tokens.
    struct token
    {
      enum yytokentype
      {
        END = 0,
        K_DEFINE = 258,
        K_DEFINEB = 259,
        K_DEFINES = 260,
        K_MESSAGE = 261,
        K_CREATEFILE = 262,
        K_OPENFILE = 263,
        K_CLOSEFILE = 264,
        K_WARNING = 265,
        K_ERROR = 266,
        K_FATALERROR = 267,
        K_ABOVE = 268,
        K_ABUT = 269,
        K_ABUTMENT = 270,
        K_ACCURRENTDENSITY = 271,
        K_ACTIVE = 272,
        K_ADJACENTCUTS = 273,
        K_ANALOG = 274,
        K_AND = 275,
        K_ANTENNAAREAFACTOR = 276,
        K_ANTENNAAREADIFFREDUCEPWL = 277,
        K_ANTENNAAREAMINUSDIFF = 278,
        K_ANTENNAAREARATIO = 279,
        K_ANTENNACELL = 280,
        K_ANTENNACUMAREARATIO = 281,
        K_ANTENNACUMDIFFAREARATIO = 282,
        K_ANTENNACUMDIFFSIDEAREARATIO = 283,
        K_ANTENNACUMROUTINGPLUSCUT = 284,
        K_ANTENNACUMSIDEAREARATIO = 285,
        K_ANTENNADIFFAREA = 286,
        K_ANTENNADIFFAREARATIO = 287,
        K_ANTENNADIFFSIDEAREARATIO = 288,
        K_ANTENNAGATEAREA = 289,
        K_ANTENNAGATEPLUSDIFF = 290,
        K_ANTENNAINOUTDIFFAREA = 291,
        K_ANTENNAINPUTGATEAREA = 292,
        K_ANTENNALENGTHFACTOR = 293,
        K_ANTENNAMAXAREACAR = 294,
        K_ANTENNAMAXCUTCAR = 295,
        K_ANTENNAMAXSIDEAREACAR = 296,
        K_ANTENNAMETALAREA = 297,
        K_ANTENNAMETALLENGTH = 298,
        K_ANTENNAMODEL = 299,
        K_ANTENNAOUTPUTDIFFAREA = 300,
        K_ANTENNAPARTIALCUTAREA = 301,
        K_ANTENNAPARTIALMETALAREA = 302,
        K_ANTENNAPARTIALMETALSIDEAREA = 303,
        K_ANTENNASIDEAREARATIO = 304,
        K_ANTENNASIZE = 305,
        K_ANTENNASIDEAREAFACTOR = 306,
        K_ANYEDGE = 307,
        K_AREA = 308,
        K_AREAIO = 309,
        K_ARRAY = 310,
        K_ARRAYCUTS = 311,
        K_ARRAYSPACING = 312,
        K_AVERAGE = 313,
        K_BELOW = 314,
        K_BEGINEXT = 315,
        K_BLACKBOX = 316,
        K_BLOCK = 317,
        K_BOTTOMLEFT = 318,
        K_BOTTOMRIGHT = 319,
        K_BUMP = 320,
        K_BUSBITCHARS = 321,
        K_BUFFER = 322,
        K_BY = 323,
        K_CANNOTOCCUPY = 324,
        K_CANPLACE = 325,
        K_CAPACITANCE = 326,
        K_CAPMULTIPLIER = 327,
        K_CENTERTOCENTER = 328,
        K_CLASS = 329,
        K_CLEARANCEMEASURE = 330,
        K_CLOCK = 331,
        K_CLOCKTYPE = 332,
        K_COLUMNMAJOR = 333,
        K_CURRENTDEN = 334,
        K_COMPONENTPIN = 335,
        K_CORE = 336,
        K_CORNER = 337,
        K_CORRECTIONFACTOR = 338,
        K_CORRECTIONTABLE = 339,
        K_COVER = 340,
        K_CPERSQDIST = 341,
        K_CURRENT = 342,
        K_CURRENTSOURCE = 343,
        K_CUT = 344,
        K_CUTAREA = 345,
        K_CUTSIZE = 346,
        K_CUTSPACING = 347,
        K_DATA = 348,
        K_DATABASE = 349,
        K_DCCURRENTDENSITY = 350,
        K_DEFAULT = 351,
        K_DEFAULTCAP = 352,
        K_DELAY = 353,
        K_DENSITY = 354,
        K_DENSITYCHECKSTEP = 355,
        K_DENSITYCHECKWINDOW = 356,
        K_DESIGNRULEWIDTH = 357,
        K_DIAG45 = 358,
        K_DIAG135 = 359,
        K_DIAGMINEDGELENGTH = 360,
        K_DIAGSPACING = 361,
        K_DIAGPITCH = 362,
        K_DIAGWIDTH = 363,
        K_DIELECTRIC = 364,
        K_DIFFUSEONLY = 365,
        K_DIRECTION = 366,
        K_DIVIDERCHAR = 367,
        K_DO = 368,
        K_E = 369,
        K_EDGECAPACITANCE = 370,
        K_EDGERATE = 371,
        K_EDGERATESCALEFACTOR = 372,
        K_EDGERATETHRESHOLD1 = 373,
        K_EDGERATETHRESHOLD2 = 374,
        K_EEQ = 375,
        K_ELSE = 376,
        K_ENCLOSURE = 377,
        K_END = 378,
        K_ENDEXT = 379,
        K_ENDCAP = 380,
        K_ENDOFLINE = 381,
        K_ENDOFNOTCHWIDTH = 382,
        K_EUCLIDEAN = 383,
        K_EXCEPTEXTRACUT = 384,
        K_EXCEPTSAMEPGNET = 385,
        K_EXCEPTPGNET = 386,
        K_EXTENSION = 387,
        K_FALL = 388,
        K_FALLCS = 389,
        K_FALLRS = 390,
        K_FALLSATCUR = 391,
        K_FALLSATT1 = 392,
        K_FALLSLEWLIMIT = 393,
        K_FALLT0 = 394,
        K_FALLTHRESH = 395,
        K_FALLVOLTAGETHRESHOLD = 396,
        K_FALSE = 397,
        K_FE = 398,
        K_FEEDTHRU = 399,
        K_FILLACTIVESPACING = 400,
        K_FIXED = 401,
        K_FLIP = 402,
        K_FLOORPLAN = 403,
        K_FN = 404,
        K_FOREIGN = 405,
        K_FREQUENCY = 406,
        K_FROMABOVE = 407,
        K_FROMBELOW = 408,
        K_FROMPIN = 409,
        K_FUNCTION = 410,
        K_FS = 411,
        K_FW = 412,
        K_GCELLGRID = 413,
        K_GENERATE = 414,
        K_GENERATED = 415,
        K_GENERATOR = 416,
        K_GROUND = 417,
        K_GROUNDSENSITIVITY = 418,
        K_HARDSPACING = 419,
        K_HEIGHT = 420,
        K_HISTORY = 421,
        K_HOLD = 422,
        K_HORIZONTAL = 423,
        K_IF = 424,
        K_IMPLANT = 425,
        K_INFLUENCE = 426,
        K_INOUT = 427,
        K_INOUTPINANTENNASIZE = 428,
        K_INPUT = 429,
        K_INPUTPINANTENNASIZE = 430,
        K_INPUTNOISEMARGIN = 431,
        K_INSIDECORNER = 432,
        K_INTEGER = 433,
        K_INTRINSIC = 434,
        K_INVERT = 435,
        K_INVERTER = 436,
        K_IRDROP = 437,
        K_ITERATE = 438,
        K_IV_TABLES = 439,
        K_LAYER = 440,
        K_LAYERS = 441,
        K_LEAKAGE = 442,
        K_LENGTH = 443,
        K_LENGTHSUM = 444,
        K_LENGTHTHRESHOLD = 445,
        K_LEQ = 446,
        K_LIBRARY = 447,
        K_LONGARRAY = 448,
        K_MACRO = 449,
        K_MANUFACTURINGGRID = 450,
        K_MASTERSLICE = 451,
        K_MATCH = 452,
        K_MAXADJACENTSLOTSPACING = 453,
        K_MAXCOAXIALSLOTSPACING = 454,
        K_MAXDELAY = 455,
        K_MAXEDGES = 456,
        K_MAXEDGESLOTSPACING = 457,
        K_MAXLOAD = 458,
        K_MAXIMUMDENSITY = 459,
        K_MAXVIASTACK = 460,
        K_MAXWIDTH = 461,
        K_MAXXY = 462,
        K_MEGAHERTZ = 463,
        K_METALOVERHANG = 464,
        K_MICRONS = 465,
        K_MILLIAMPS = 466,
        K_MILLIWATTS = 467,
        K_MINCUTS = 468,
        K_MINENCLOSEDAREA = 469,
        K_MINFEATURE = 470,
        K_MINIMUMCUT = 471,
        K_MINIMUMDENSITY = 472,
        K_MINPINS = 473,
        K_MINSIZE = 474,
        K_MINSTEP = 475,
        K_MINWIDTH = 476,
        K_MPWH = 477,
        K_MPWL = 478,
        K_MUSTJOIN = 479,
        K_MX = 480,
        K_MY = 481,
        K_MXR90 = 482,
        K_MYR90 = 483,
        K_N = 484,
        K_NAMEMAPSTRING = 485,
        K_NAMESCASESENSITIVE = 486,
        K_NANOSECONDS = 487,
        K_NEGEDGE = 488,
        K_NETEXPR = 489,
        K_NETS = 490,
        K_NEW = 491,
        K_NONDEFAULTRULE = 492,
        K_NONE = 493,
        K_NONINVERT = 494,
        K_NONUNATE = 495,
        K_NOISETABLE = 496,
        K_NOTCHLENGTH = 497,
        K_NOTCHSPACING = 498,
        K_NOWIREEXTENSIONATPIN = 499,
        K_OBS = 500,
        K_OFF = 501,
        K_OFFSET = 502,
        K_OHMS = 503,
        K_ON = 504,
        K_OR = 505,
        K_ORIENT = 506,
        K_ORIENTATION = 507,
        K_ORIGIN = 508,
        K_ORTHOGONAL = 509,
        K_OUTPUT = 510,
        K_OUTPUTPINANTENNASIZE = 511,
        K_OUTPUTNOISEMARGIN = 512,
        K_OUTPUTRESISTANCE = 513,
        K_OUTSIDECORNER = 514,
        K_OVERHANG = 515,
        K_OVERLAP = 516,
        K_OVERLAPS = 517,
        K_OXIDE1 = 518,
        K_OXIDE2 = 519,
        K_OXIDE3 = 520,
        K_OXIDE4 = 521,
        K_PAD = 522,
        K_PARALLELEDGE = 523,
        K_PARALLELOVERLAP = 524,
        K_PARALLELRUNLENGTH = 525,
        K_PATH = 526,
        K_PATTERN = 527,
        K_PEAK = 528,
        K_PERIOD = 529,
        K_PGONLY = 530,
        K_PICOFARADS = 531,
        K_PIN = 532,
        K_PITCH = 533,
        K_PLACED = 534,
        K_POLYGON = 535,
        K_PORT = 536,
        K_POSEDGE = 537,
        K_POST = 538,
        K_POWER = 539,
        K_PRE = 540,
        K_PREFERENCLOSURE = 541,
        K_PRL = 542,
        K_PROPERTY = 543,
        K_PROPERTYDEFINITIONS = 544,
        K_PROTRUSIONWIDTH = 545,
        K_PULLDOWNRES = 546,
        K_PWL = 547,
        K_R0 = 548,
        K_R90 = 549,
        K_R180 = 550,
        K_R270 = 551,
        K_RANGE = 552,
        K_REAL = 553,
        K_RECOVERY = 554,
        K_RECT = 555,
        K_RESISTANCE = 556,
        K_RESISTIVE = 557,
        K_RING = 558,
        K_RISE = 559,
        K_RISECS = 560,
        K_RISERS = 561,
        K_RISESATCUR = 562,
        K_RISESATT1 = 563,
        K_RISESLEWLIMIT = 564,
        K_RISET0 = 565,
        K_RISETHRESH = 566,
        K_RISEVOLTAGETHRESHOLD = 567,
        K_RMS = 568,
        K_ROUTING = 569,
        K_ROWABUTSPACING = 570,
        K_ROWCOL = 571,
        K_ROWMAJOR = 572,
        K_ROWMINSPACING = 573,
        K_ROWPATTERN = 574,
        K_RPERSQ = 575,
        K_S = 576,
        K_SAMENET = 577,
        K_SCANUSE = 578,
        K_SDFCOND = 579,
        K_SDFCONDEND = 580,
        K_SDFCONDSTART = 581,
        K_SETUP = 582,
        K_SHAPE = 583,
        K_SHRINKAGE = 584,
        K_SIGNAL = 585,
        K_SITE = 586,
        K_SIZE = 587,
        K_SKEW = 588,
        K_SLOTLENGTH = 589,
        K_SLOTWIDTH = 590,
        K_SLOTWIRELENGTH = 591,
        K_SLOTWIREWIDTH = 592,
        K_SPLITWIREWIDTH = 593,
        K_SOFT = 594,
        K_SOURCE = 595,
        K_SPACER = 596,
        K_SPACING = 597,
        K_SPACINGTABLE = 598,
        K_SPECIALNETS = 599,
        K_STABLE = 600,
        K_STACK = 601,
        K_START = 602,
        K_STEP = 603,
        K_STOP = 604,
        K_STRING = 605,
        K_STRUCTURE = 606,
        K_SUPPLYSENSITIVITY = 607,
        K_SYMMETRY = 608,
        K_TABLE = 609,
        K_TABLEAXIS = 610,
        K_TABLEDIMENSION = 611,
        K_TABLEENTRIES = 612,
        K_TAPERRULE = 613,
        K_THEN = 614,
        K_THICKNESS = 615,
        K_TIEHIGH = 616,
        K_TIELOW = 617,
        K_TIEOFFR = 618,
        K_TIME = 619,
        K_TIMING = 620,
        K_TO = 621,
        K_TOPIN = 622,
        K_TOPLEFT = 623,
        K_TOPOFSTACKONLY = 624,
        K_TOPRIGHT = 625,
        K_TRACKS = 626,
        K_TRANSITIONTIME = 627,
        K_TRISTATE = 628,
        K_TRUE = 629,
        K_TWOEDGES = 630,
        K_TWOWIDTHS = 631,
        K_TYPE = 632,
        K_UNATENESS = 633,
        K_UNITS = 634,
        K_UNIVERSALNOISEMARGIN = 635,
        K_USE = 636,
        K_USELENGTHTHRESHOLD = 637,
        K_USEMINSPACING = 638,
        K_USER = 639,
        K_USEVIA = 640,
        K_USEVIARULE = 641,
        K_VARIABLE = 642,
        K_VERSION = 643,
        K_VERTICAL = 644,
        K_VHI = 645,
        K_VIA = 646,
        K_VIARULE = 647,
        K_VICTIMLENGTH = 648,
        K_VICTIMNOISE = 649,
        K_VIRTUAL = 650,
        K_VLO = 651,
        K_VOLTAGE = 652,
        K_VOLTS = 653,
        K_W = 654,
        K_WELLTAP = 655,
        K_WIDTH = 656,
        K_WITHIN = 657,
        K_WIRECAP = 658,
        K_WIREEXTENSION = 659,
        K_X = 660,
        K_Y = 661,
        K_EQ = 662,
        K_NE = 663,
        K_LE = 664,
        K_LT = 665,
        K_GE = 666,
        K_GT = 667,
        K_NOT = 668,
        INTEGER = 669,
        DOUBLE = 670,
        STRING = 671,
        QSTRING = 672,
        BINARY = 673,
        IF = 676,
        LNOT = 677,
        UMINUS = 678
      };
    };

    /// (External) token type, as returned by yylex.
    typedef token::yytokentype token_type;

    /// Symbol type: an internal symbol number.
    typedef int symbol_number_type;

    /// The symbol type number to denote an empty symbol.
    enum { empty_symbol = -2 };

    /// Internal symbol number for tokens (subsumed by symbol_number_type).
    typedef short token_number_type;

    /// A complete symbol.
    ///
    /// Expects its Base type to provide access to the symbol type
    /// via type_get ().
    ///
    /// Provide access to semantic value and location.
    template <typename Base>
    struct basic_symbol : Base
    {
      /// Alias to Base.
      typedef Base super_type;

      /// Default constructor.
      basic_symbol ()
        : value ()
        , location ()
      {}

#if 201103L <= YY_CPLUSPLUS
      /// Move constructor.
      basic_symbol (basic_symbol&& that);
#endif

      /// Copy constructor.
      basic_symbol (const basic_symbol& that);
      /// Constructor for valueless symbols.
      basic_symbol (typename Base::kind_type t,
                    YY_MOVE_REF (location_type) l);

      /// Constructor for symbols with semantic value.
      basic_symbol (typename Base::kind_type t,
                    YY_RVREF (semantic_type) v,
                    YY_RVREF (location_type) l);

      /// Destroy the symbol.
      ~basic_symbol ()
      {
        clear ();
      }

      /// Destroy contents, and record that is empty.
      void clear ()
      {
        Base::clear ();
      }

      /// Whether empty.
      bool empty () const YY_NOEXCEPT;

      /// Destructive move, \a s is emptied into this.
      void move (basic_symbol& s);

      /// The semantic value.
      semantic_type value;

      /// The location.
      location_type location;

    private:
#if YY_CPLUSPLUS < 201103L
      /// Assignment operator.
      basic_symbol& operator= (const basic_symbol& that);
#endif
    };

    /// Type access provider for token (enum) based symbols.
    struct by_type
    {
      /// Default constructor.
      by_type ();

#if 201103L <= YY_CPLUSPLUS
      /// Move constructor.
      by_type (by_type&& that);
#endif

      /// Copy constructor.
      by_type (const by_type& that);

      /// The symbol type as needed by the constructor.
      typedef token_type kind_type;

      /// Constructor from (external) token numbers.
      by_type (kind_type t);

      /// Record that this symbol is empty.
      void clear ();

      /// Steal the symbol type from \a that.
      void move (by_type& that);

      /// The (internal) type number (corresponding to \a type).
      /// \a empty when empty.
      symbol_number_type type_get () const YY_NOEXCEPT;

      /// The symbol type.
      /// \a empty_symbol when empty.
      /// An int, not token_number_type, to be able to store empty_symbol.
      int type;
    };

    /// "External" symbols: returned by the scanner.
    struct symbol_type : basic_symbol<by_type>
    {};

    /// Build a parser object.
    Parser (class Driver& driver_yyarg);
    virtual ~Parser ();

    /// Parse.  An alias for parse ().
    /// \returns  0 iff parsing succeeded.
    int operator() ();

    /// Parse.
    /// \returns  0 iff parsing succeeded.
    virtual int parse ();

#if LEFPARSERDEBUG
    /// The current debugging stream.
    std::ostream& debug_stream () const YY_ATTRIBUTE_PURE;
    /// Set the current debugging stream.
    void set_debug_stream (std::ostream &);

    /// Type for debugging levels.
    typedef int debug_level_type;
    /// The current debugging level.
    debug_level_type debug_level () const YY_ATTRIBUTE_PURE;
    /// Set the current debugging level.
    void set_debug_level (debug_level_type l);
#endif

    /// Report a syntax error.
    /// \param loc    where the syntax error is found.
    /// \param msg    a description of the syntax error.
    virtual void error (const location_type& loc, const std::string& msg);

    /// Report a syntax error.
    void error (const syntax_error& err);



  private:
    /// This class is not copyable.
    Parser (const Parser&);
    Parser& operator= (const Parser&);

    /// Stored state numbers (used for stacks).
    typedef short state_type;

    /// Generate an error message.
    /// \param yystate   the state where the error occurred.
    /// \param yyla      the lookahead token.
    virtual std::string yysyntax_error_ (state_type yystate,
                                         const symbol_type& yyla) const;

    /// Compute post-reduction state.
    /// \param yystate   the current state
    /// \param yysym     the nonterminal to push on the stack
    static state_type yy_lr_goto_state_ (state_type yystate, int yysym);

    /// Whether the given \c yypact_ value indicates a defaulted state.
    /// \param yyvalue   the value to check
    static bool yy_pact_value_is_default_ (int yyvalue);

    /// Whether the given \c yytable_ value indicates a syntax error.
    /// \param yyvalue   the value to check
    static bool yy_table_value_is_error_ (int yyvalue);

    static const short yypact_ninf_;
    static const short yytable_ninf_;

    /// Convert a scanner token number \a t to a symbol number.
    /// In theory \a t should be a token_type, but character literals
    /// are valid, yet not members of the token_type enum.
    static token_number_type yytranslate_ (int t);

    // Tables.
    // YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
    // STATE-NUM.
    static const short yypact_[];

    // YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
    // Performed when YYTABLE does not specify something else to do.  Zero
    // means the default is an error.
    static const short yydefact_[];

    // YYPGOTO[NTERM-NUM].
    static const short yypgoto_[];

    // YYDEFGOTO[NTERM-NUM].
    static const short yydefgoto_[];

    // YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
    // positive, shift that token.  If negative, reduce the rule whose
    // number is the opposite.  If YYTABLE_NINF, syntax error.
    static const short yytable_[];

    static const short yycheck_[];

    // YYSTOS[STATE-NUM] -- The (internal number of the) accessing
    // symbol of state STATE-NUM.
    static const short yystos_[];

    // YYR1[YYN] -- Symbol number of symbol that rule YYN derives.
    static const short yyr1_[];

    // YYR2[YYN] -- Number of symbols on the right hand side of rule YYN.
    static const signed char yyr2_[];


    /// Convert the symbol name \a n to a form suitable for a diagnostic.
    static std::string yytnamerr_ (const char *n);


    /// For a symbol, its name in clear.
    static const char* const yytname_[];
#if LEFPARSERDEBUG
    // YYRLINE[YYN] -- Source line where rule number YYN was defined.
    static const short yyrline_[];
    /// Report on the debug stream that the rule \a r is going to be reduced.
    virtual void yy_reduce_print_ (int r);
    /// Print the state stack on the debug stream.
    virtual void yystack_print_ ();

    /// Debugging level.
    int yydebug_;
    /// Debug stream.
    std::ostream* yycdebug_;

    /// \brief Display a symbol type, value and location.
    /// \param yyo    The output stream.
    /// \param yysym  The symbol.
    template <typename Base>
    void yy_print_ (std::ostream& yyo, const basic_symbol<Base>& yysym) const;
#endif

    /// \brief Reclaim the memory associated to a symbol.
    /// \param yymsg     Why this token is reclaimed.
    ///                  If null, print nothing.
    /// \param yysym     The symbol.
    template <typename Base>
    void yy_destroy_ (const char* yymsg, basic_symbol<Base>& yysym) const;

  private:
    /// Type access provider for state based symbols.
    struct by_state
    {
      /// Default constructor.
      by_state () YY_NOEXCEPT;

      /// The symbol type as needed by the constructor.
      typedef state_type kind_type;

      /// Constructor.
      by_state (kind_type s) YY_NOEXCEPT;

      /// Copy constructor.
      by_state (const by_state& that) YY_NOEXCEPT;

      /// Record that this symbol is empty.
      void clear () YY_NOEXCEPT;

      /// Steal the symbol type from \a that.
      void move (by_state& that);

      /// The (internal) type number (corresponding to \a state).
      /// \a empty_symbol when empty.
      symbol_number_type type_get () const YY_NOEXCEPT;

      /// The state number used to denote an empty symbol.
      /// We use the initial state, as it does not have a value.
      enum { empty_state = 0 };

      /// The state.
      /// \a empty when empty.
      state_type state;
    };

    /// "Internal" symbol: element of the stack.
    struct stack_symbol_type : basic_symbol<by_state>
    {
      /// Superclass.
      typedef basic_symbol<by_state> super_type;
      /// Construct an empty symbol.
      stack_symbol_type ();
      /// Move or copy construction.
      stack_symbol_type (YY_RVREF (stack_symbol_type) that);
      /// Steal the contents from \a sym to build this.
      stack_symbol_type (state_type s, YY_MOVE_REF (symbol_type) sym);
#if YY_CPLUSPLUS < 201103L
      /// Assignment, needed by push_back by some old implementations.
      /// Moves the contents of that.
      stack_symbol_type& operator= (stack_symbol_type& that);

      /// Assignment, needed by push_back by other implementations.
      /// Needed by some other old implementations.
      stack_symbol_type& operator= (const stack_symbol_type& that);
#endif
    };

    /// A stack with random access from its top.
    template <typename T, typename S = std::vector<T> >
    class stack
    {
    public:
      // Hide our reversed order.
      typedef typename S::reverse_iterator iterator;
      typedef typename S::const_reverse_iterator const_iterator;
      typedef typename S::size_type size_type;
      typedef typename std::ptrdiff_t index_type;

      stack (size_type n = 200)
        : seq_ (n)
      {}

      /// Random access.
      ///
      /// Index 0 returns the topmost element.
      const T&
      operator[] (index_type i) const
      {
        return seq_[size_type (size () - 1 - i)];
      }

      /// Random access.
      ///
      /// Index 0 returns the topmost element.
      T&
      operator[] (index_type i)
      {
        return seq_[size_type (size () - 1 - i)];
      }

      /// Steal the contents of \a t.
      ///
      /// Close to move-semantics.
      void
      push (YY_MOVE_REF (T) t)
      {
        seq_.push_back (T ());
        operator[] (0).move (t);
      }

      /// Pop elements from the stack.
      void
      pop (std::ptrdiff_t n = 1) YY_NOEXCEPT
      {
        for (; 0 < n; --n)
          seq_.pop_back ();
      }

      /// Pop all elements from the stack.
      void
      clear () YY_NOEXCEPT
      {
        seq_.clear ();
      }

      /// Number of elements on the stack.
      index_type
      size () const YY_NOEXCEPT
      {
        return index_type (seq_.size ());
      }

      std::ptrdiff_t
      ssize () const YY_NOEXCEPT
      {
        return std::ptrdiff_t (size ());
      }

      /// Iterator on top of the stack (going downwards).
      const_iterator
      begin () const YY_NOEXCEPT
      {
        return seq_.rbegin ();
      }

      /// Bottom of the stack.
      const_iterator
      end () const YY_NOEXCEPT
      {
        return seq_.rend ();
      }

      /// Present a slice of the top of a stack.
      class slice
      {
      public:
        slice (const stack& stack, index_type range)
          : stack_ (stack)
          , range_ (range)
        {}

        const T&
        operator[] (index_type i) const
        {
          return stack_[range_ - i];
        }

      private:
        const stack& stack_;
        index_type range_;
      };

    private:
      stack (const stack&);
      stack& operator= (const stack&);
      /// The wrapped container.
      S seq_;
    };


    /// Stack type.
    typedef stack<stack_symbol_type> stack_type;

    /// The stack.
    stack_type yystack_;

    /// Push a new state on the stack.
    /// \param m    a debug message to display
    ///             if null, no trace is output.
    /// \param sym  the symbol
    /// \warning the contents of \a s.value is stolen.
    void yypush_ (const char* m, YY_MOVE_REF (stack_symbol_type) sym);

    /// Push a new look ahead token on the state on the stack.
    /// \param m    a debug message to display
    ///             if null, no trace is output.
    /// \param s    the state
    /// \param sym  the symbol (for its value and location).
    /// \warning the contents of \a sym.value is stolen.
    void yypush_ (const char* m, state_type s, YY_MOVE_REF (symbol_type) sym);

    /// Pop \a n symbols from the stack.
    void yypop_ (int n = 1);

    /// Some specific tokens.
    static const token_number_type yy_error_token_ = 1;
    static const token_number_type yy_undef_token_ = 2;

    /// Constants.
    enum
    {
      yyeof_ = 0,
      yylast_ = 3683,     ///< Last index in yytable_.
      yynnts_ = 448,  ///< Number of nonterminal symbols.
      yyfinal_ = 4, ///< Termination state number.
      yyntokens_ = 435  ///< Number of tokens.
    };


    // User arguments.
    class Driver& driver;
  };


} // LefParser
#line 1132 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/lef/bison/LefParser.h"





#endif // !YY_LEFPARSER_HOME_MSEARS_AIEPLACE_CPP_LIMBO_BUILD_LIMBO_PARSERS_LEF_BISON_LEFPARSER_H_INCLUDED
