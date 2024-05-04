// A Bison parser, made by GNU Bison 3.5.1.

// Skeleton implementation for Bison LALR(1) parsers in C++

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

// Undocumented macros, especially those whose name start with YY_,
// are private implementation details.  Do not rely on them.


// Take the name prefix into account.
#define yylex   GdfParserlex

// First part of user prologue.
#line 4 "GdfParser.yy"
 /*** C/C++ Declarations ***/

#include <stdio.h>
#include <string>
#include <vector>

/*#include "expression.h"*/


#line 52 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"


#include "GdfParser.h"

// Second part of user prologue.
#line 125 "GdfParser.yy"


#include "GdfDriver.h"
#include "GdfScanner.h"

/* this "connects" the bison parser in the driver to the flex scanner class
 * object. it defines the yylex() function call to pull the next token from the
 * current lexer object of the driver context. */
#undef yylex
#define yylex driver.lexer->lex


#line 71 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"



#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> // FIXME: INFRINGES ON USER NAME SPACE.
#   define YY_(msgid) dgettext ("bison-runtime", msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(msgid) msgid
# endif
#endif

// Whether we are compiled with exception support.
#ifndef YY_EXCEPTIONS
# if defined __GNUC__ && !defined __EXCEPTIONS
#  define YY_EXCEPTIONS 0
# else
#  define YY_EXCEPTIONS 1
# endif
#endif

#define YYRHSLOC(Rhs, K) ((Rhs)[K].location)
/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

# ifndef YYLLOC_DEFAULT
#  define YYLLOC_DEFAULT(Current, Rhs, N)                               \
    do                                                                  \
      if (N)                                                            \
        {                                                               \
          (Current).begin  = YYRHSLOC (Rhs, 1).begin;                   \
          (Current).end    = YYRHSLOC (Rhs, N).end;                     \
        }                                                               \
      else                                                              \
        {                                                               \
          (Current).begin = (Current).end = YYRHSLOC (Rhs, 0).end;      \
        }                                                               \
    while (false)
# endif


// Enable debugging if requested.
#if GDFPARSERDEBUG

// A pseudo ostream that takes yydebug_ into account.
# define YYCDEBUG if (yydebug_) (*yycdebug_)

# define YY_SYMBOL_PRINT(Title, Symbol)         \
  do {                                          \
    if (yydebug_)                               \
    {                                           \
      *yycdebug_ << Title << ' ';               \
      yy_print_ (*yycdebug_, Symbol);           \
      *yycdebug_ << '\n';                       \
    }                                           \
  } while (false)

# define YY_REDUCE_PRINT(Rule)          \
  do {                                  \
    if (yydebug_)                       \
      yy_reduce_print_ (Rule);          \
  } while (false)

# define YY_STACK_PRINT()               \
  do {                                  \
    if (yydebug_)                       \
      yystack_print_ ();                \
  } while (false)

#else // !GDFPARSERDEBUG

# define YYCDEBUG if (false) std::cerr
# define YY_SYMBOL_PRINT(Title, Symbol)  YYUSE (Symbol)
# define YY_REDUCE_PRINT(Rule)           static_cast<void> (0)
# define YY_STACK_PRINT()                static_cast<void> (0)

#endif // !GDFPARSERDEBUG

#define yyerrok         (yyerrstatus_ = 0)
#define yyclearin       (yyla.clear ())

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYRECOVERING()  (!!yyerrstatus_)

namespace GdfParser {
#line 163 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"


  /* Return YYSTR after stripping away unnecessary quotes and
     backslashes, so that it's suitable for yyerror.  The heuristic is
     that double-quoting is unnecessary unless the string contains an
     apostrophe, a comma, or backslash (other than backslash-backslash).
     YYSTR is taken from yytname.  */
  std::string
  Parser::yytnamerr_ (const char *yystr)
  {
    if (*yystr == '"')
      {
        std::string yyr;
        char const *yyp = yystr;

        for (;;)
          switch (*++yyp)
            {
            case '\'':
            case ',':
              goto do_not_strip_quotes;

            case '\\':
              if (*++yyp != '\\')
                goto do_not_strip_quotes;
              else
                goto append;

            append:
            default:
              yyr += *yyp;
              break;

            case '"':
              return yyr;
            }
      do_not_strip_quotes: ;
      }

    return yystr;
  }


  /// Build a parser object.
  Parser::Parser (class Driver& driver_yyarg)
#if GDFPARSERDEBUG
    : yydebug_ (false),
      yycdebug_ (&std::cerr),
#else
    :
#endif
      driver (driver_yyarg)
  {}

  Parser::~Parser ()
  {}

  Parser::syntax_error::~syntax_error () YY_NOEXCEPT YY_NOTHROW
  {}

  /*---------------.
  | Symbol types.  |
  `---------------*/

  // basic_symbol.
#if 201103L <= YY_CPLUSPLUS
  template <typename Base>
  Parser::basic_symbol<Base>::basic_symbol (basic_symbol&& that)
    : Base (std::move (that))
    , value (std::move (that.value))
    , location (std::move (that.location))
  {}
#endif

  template <typename Base>
  Parser::basic_symbol<Base>::basic_symbol (const basic_symbol& that)
    : Base (that)
    , value (that.value)
    , location (that.location)
  {}


  /// Constructor for valueless symbols.
  template <typename Base>
  Parser::basic_symbol<Base>::basic_symbol (typename Base::kind_type t, YY_MOVE_REF (location_type) l)
    : Base (t)
    , value ()
    , location (l)
  {}

  template <typename Base>
  Parser::basic_symbol<Base>::basic_symbol (typename Base::kind_type t, YY_RVREF (semantic_type) v, YY_RVREF (location_type) l)
    : Base (t)
    , value (YY_MOVE (v))
    , location (YY_MOVE (l))
  {}

  template <typename Base>
  bool
  Parser::basic_symbol<Base>::empty () const YY_NOEXCEPT
  {
    return Base::type_get () == empty_symbol;
  }

  template <typename Base>
  void
  Parser::basic_symbol<Base>::move (basic_symbol& s)
  {
    super_type::move (s);
    value = YY_MOVE (s.value);
    location = YY_MOVE (s.location);
  }

  // by_type.
  Parser::by_type::by_type ()
    : type (empty_symbol)
  {}

#if 201103L <= YY_CPLUSPLUS
  Parser::by_type::by_type (by_type&& that)
    : type (that.type)
  {
    that.clear ();
  }
#endif

  Parser::by_type::by_type (const by_type& that)
    : type (that.type)
  {}

  Parser::by_type::by_type (token_type t)
    : type (yytranslate_ (t))
  {}

  void
  Parser::by_type::clear ()
  {
    type = empty_symbol;
  }

  void
  Parser::by_type::move (by_type& that)
  {
    type = that.type;
    that.clear ();
  }

  int
  Parser::by_type::type_get () const YY_NOEXCEPT
  {
    return type;
  }


  // by_state.
  Parser::by_state::by_state () YY_NOEXCEPT
    : state (empty_state)
  {}

  Parser::by_state::by_state (const by_state& that) YY_NOEXCEPT
    : state (that.state)
  {}

  void
  Parser::by_state::clear () YY_NOEXCEPT
  {
    state = empty_state;
  }

  void
  Parser::by_state::move (by_state& that)
  {
    state = that.state;
    that.clear ();
  }

  Parser::by_state::by_state (state_type s) YY_NOEXCEPT
    : state (s)
  {}

  Parser::symbol_number_type
  Parser::by_state::type_get () const YY_NOEXCEPT
  {
    if (state == empty_state)
      return empty_symbol;
    else
      return yystos_[+state];
  }

  Parser::stack_symbol_type::stack_symbol_type ()
  {}

  Parser::stack_symbol_type::stack_symbol_type (YY_RVREF (stack_symbol_type) that)
    : super_type (YY_MOVE (that.state), YY_MOVE (that.value), YY_MOVE (that.location))
  {
#if 201103L <= YY_CPLUSPLUS
    // that is emptied.
    that.state = empty_state;
#endif
  }

  Parser::stack_symbol_type::stack_symbol_type (state_type s, YY_MOVE_REF (symbol_type) that)
    : super_type (s, YY_MOVE (that.value), YY_MOVE (that.location))
  {
    // that is emptied.
    that.type = empty_symbol;
  }

#if YY_CPLUSPLUS < 201103L
  Parser::stack_symbol_type&
  Parser::stack_symbol_type::operator= (const stack_symbol_type& that)
  {
    state = that.state;
    value = that.value;
    location = that.location;
    return *this;
  }

  Parser::stack_symbol_type&
  Parser::stack_symbol_type::operator= (stack_symbol_type& that)
  {
    state = that.state;
    value = that.value;
    location = that.location;
    // that is emptied.
    that.state = empty_state;
    return *this;
  }
#endif

  template <typename Base>
  void
  Parser::yy_destroy_ (const char* yymsg, basic_symbol<Base>& yysym) const
  {
    if (yymsg)
      YY_SYMBOL_PRINT (yymsg, yysym);

    // User destructor.
    switch (yysym.type_get ())
    {
      case 5: // "string"
#line 120 "GdfParser.yy"
                    { delete (yysym.value.stringVal); }
#line 407 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
        break;

      case 6: // "quoted chars"
#line 120 "GdfParser.yy"
                    { delete (yysym.value.quoteVal); }
#line 413 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
        break;

      case 8: // "binary numbers"
#line 120 "GdfParser.yy"
                    { delete (yysym.value.binaryVal); }
#line 419 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
        break;

      default:
        break;
    }
  }

#if GDFPARSERDEBUG
  template <typename Base>
  void
  Parser::yy_print_ (std::ostream& yyo,
                                     const basic_symbol<Base>& yysym) const
  {
    std::ostream& yyoutput = yyo;
    YYUSE (yyoutput);
    symbol_number_type yytype = yysym.type_get ();
#if defined __GNUC__ && ! defined __clang__ && ! defined __ICC && __GNUC__ * 100 + __GNUC_MINOR__ <= 408
    // Avoid a (spurious) G++ 4.8 warning about "array subscript is
    // below array bounds".
    if (yysym.empty ())
      std::abort ();
#endif
    yyo << (yytype < yyntokens_ ? "token" : "nterm")
        << ' ' << yytname_[yytype] << " ("
        << yysym.location << ": ";
    YYUSE (yytype);
    yyo << ')';
  }
#endif

  void
  Parser::yypush_ (const char* m, YY_MOVE_REF (stack_symbol_type) sym)
  {
    if (m)
      YY_SYMBOL_PRINT (m, sym);
    yystack_.push (YY_MOVE (sym));
  }

  void
  Parser::yypush_ (const char* m, state_type s, YY_MOVE_REF (symbol_type) sym)
  {
#if 201103L <= YY_CPLUSPLUS
    yypush_ (m, stack_symbol_type (s, std::move (sym)));
#else
    stack_symbol_type ss (s, sym);
    yypush_ (m, ss);
#endif
  }

  void
  Parser::yypop_ (int n)
  {
    yystack_.pop (n);
  }

#if GDFPARSERDEBUG
  std::ostream&
  Parser::debug_stream () const
  {
    return *yycdebug_;
  }

  void
  Parser::set_debug_stream (std::ostream& o)
  {
    yycdebug_ = &o;
  }


  Parser::debug_level_type
  Parser::debug_level () const
  {
    return yydebug_;
  }

  void
  Parser::set_debug_level (debug_level_type l)
  {
    yydebug_ = l;
  }
#endif // GDFPARSERDEBUG

  Parser::state_type
  Parser::yy_lr_goto_state_ (state_type yystate, int yysym)
  {
    int yyr = yypgoto_[yysym - yyntokens_] + yystate;
    if (0 <= yyr && yyr <= yylast_ && yycheck_[yyr] == yystate)
      return yytable_[yyr];
    else
      return yydefgoto_[yysym - yyntokens_];
  }

  bool
  Parser::yy_pact_value_is_default_ (int yyvalue)
  {
    return yyvalue == yypact_ninf_;
  }

  bool
  Parser::yy_table_value_is_error_ (int yyvalue)
  {
    return yyvalue == yytable_ninf_;
  }

  int
  Parser::operator() ()
  {
    return parse ();
  }

  int
  Parser::parse ()
  {
    int yyn;
    /// Length of the RHS of the rule being reduced.
    int yylen = 0;

    // Error handling.
    int yynerrs_ = 0;
    int yyerrstatus_ = 0;

    /// The lookahead symbol.
    symbol_type yyla;

    /// The locations where the error started and ended.
    stack_symbol_type yyerror_range[3];

    /// The return value of parse ().
    int yyresult;

#if YY_EXCEPTIONS
    try
#endif // YY_EXCEPTIONS
      {
    YYCDEBUG << "Starting parse\n";


    // User initialization code.
#line 41 "GdfParser.yy"
{
    // initialize the initial location object
    yyla.location.begin.filename = yyla.location.end.filename = &driver.streamname;
}

#line 564 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"


    /* Initialize the stack.  The initial state will be set in
       yynewstate, since the latter expects the semantical and the
       location values to have been already stored, initialize these
       stacks with a primary value.  */
    yystack_.clear ();
    yypush_ (YY_NULLPTR, 0, YY_MOVE (yyla));

  /*-----------------------------------------------.
  | yynewstate -- push a new symbol on the stack.  |
  `-----------------------------------------------*/
  yynewstate:
    YYCDEBUG << "Entering state " << int (yystack_[0].state) << '\n';

    // Accept?
    if (yystack_[0].state == yyfinal_)
      YYACCEPT;

    goto yybackup;


  /*-----------.
  | yybackup.  |
  `-----------*/
  yybackup:
    // Try to take a decision without lookahead.
    yyn = yypact_[+yystack_[0].state];
    if (yy_pact_value_is_default_ (yyn))
      goto yydefault;

    // Read a lookahead token.
    if (yyla.empty ())
      {
        YYCDEBUG << "Reading a token: ";
#if YY_EXCEPTIONS
        try
#endif // YY_EXCEPTIONS
          {
            yyla.type = yytranslate_ (yylex (&yyla.value, &yyla.location));
          }
#if YY_EXCEPTIONS
        catch (const syntax_error& yyexc)
          {
            YYCDEBUG << "Caught exception: " << yyexc.what() << '\n';
            error (yyexc);
            goto yyerrlab1;
          }
#endif // YY_EXCEPTIONS
      }
    YY_SYMBOL_PRINT ("Next token is", yyla);

    /* If the proper action on seeing token YYLA.TYPE is to reduce or
       to detect an error, take that action.  */
    yyn += yyla.type_get ();
    if (yyn < 0 || yylast_ < yyn || yycheck_[yyn] != yyla.type_get ())
      {
        goto yydefault;
      }

    // Reduce or error.
    yyn = yytable_[yyn];
    if (yyn <= 0)
      {
        if (yy_table_value_is_error_ (yyn))
          goto yyerrlab;
        yyn = -yyn;
        goto yyreduce;
      }

    // Count tokens shifted since error; after three, turn off error status.
    if (yyerrstatus_)
      --yyerrstatus_;

    // Shift the lookahead token.
    yypush_ ("Shifting", state_type (yyn), YY_MOVE (yyla));
    goto yynewstate;


  /*-----------------------------------------------------------.
  | yydefault -- do the default action for the current state.  |
  `-----------------------------------------------------------*/
  yydefault:
    yyn = yydefact_[+yystack_[0].state];
    if (yyn == 0)
      goto yyerrlab;
    goto yyreduce;


  /*-----------------------------.
  | yyreduce -- do a reduction.  |
  `-----------------------------*/
  yyreduce:
    yylen = yyr2_[yyn];
    {
      stack_symbol_type yylhs;
      yylhs.state = yy_lr_goto_state_ (yystack_[yylen].state, yyr1_[yyn]);
      /* If YYLEN is nonzero, implement the default value of the
         action: '$$ = $1'.  Otherwise, use the top of the stack.

         Otherwise, the following line sets YYLHS.VALUE to garbage.
         This behavior is undocumented and Bison users should not rely
         upon it.  */
      if (yylen)
        yylhs.value = yystack_[yylen - 1].value;
      else
        yylhs.value = yystack_[0].value;

      // Default location.
      {
        stack_type::slice range (yystack_, yylen);
        YYLLOC_DEFAULT (yylhs.location, range, yylen);
        yyerror_range[1].location = yylhs.location;
      }

      // Perform the reduction.
      YY_REDUCE_PRINT (yyn);
#if YY_EXCEPTIONS
      try
#endif // YY_EXCEPTIONS
        {
          switch (yyn)
            {
  case 2:
#line 142 "GdfParser.yy"
                 {(yylhs.value.numberVal) = (yystack_[0].value.integerVal);}
#line 691 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
    break;

  case 3:
#line 143 "GdfParser.yy"
                {(yylhs.value.numberVal) = (yystack_[0].value.doubleVal);}
#line 697 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
    break;

  case 4:
#line 166 "GdfParser.yy"
                                                                                                                       {
           driver.cellPortCbk(*(yystack_[14].value.stringVal), CellPort::IN, *(yystack_[7].value.stringVal), (yystack_[3].value.numberVal), (yystack_[2].value.numberVal));
           delete (yystack_[14].value.stringVal);
           delete (yystack_[7].value.stringVal);
           }
#line 707 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
    break;

  case 5:
#line 171 "GdfParser.yy"
                                                                                                                       {
           driver.cellPortCbk(*(yystack_[14].value.stringVal), CellPort::OUT, *(yystack_[7].value.stringVal), (yystack_[3].value.numberVal), (yystack_[2].value.numberVal));
           delete (yystack_[14].value.stringVal);
           delete (yystack_[7].value.stringVal);
           }
#line 717 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
    break;

  case 6:
#line 176 "GdfParser.yy"
                                                                                                                         {
           driver.cellPortCbk(*(yystack_[14].value.stringVal), CellPort::INOUT, *(yystack_[7].value.stringVal), (yystack_[3].value.numberVal), (yystack_[2].value.numberVal));
           delete (yystack_[14].value.stringVal);
           delete (yystack_[7].value.stringVal);
           }
#line 727 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
    break;

  case 7:
#line 181 "GdfParser.yy"
                                                                                                                         {
           driver.cellPortCbk(*(yystack_[14].value.stringVal), CellPort::POWER, *(yystack_[7].value.stringVal), (yystack_[3].value.numberVal), (yystack_[2].value.numberVal));
           delete (yystack_[14].value.stringVal);
           delete (yystack_[7].value.stringVal);
           }
#line 737 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
    break;

  case 8:
#line 186 "GdfParser.yy"
                                                                                                                          {
           driver.cellPortCbk(*(yystack_[14].value.stringVal), CellPort::GROUND, *(yystack_[7].value.stringVal), (yystack_[3].value.numberVal), (yystack_[2].value.numberVal));
           delete (yystack_[14].value.stringVal);
           delete (yystack_[7].value.stringVal);
           }
#line 747 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
    break;

  case 9:
#line 193 "GdfParser.yy"
                                                                                                                                    {
               driver.cellInstanceCbk(*(yystack_[14].value.stringVal), *(yystack_[11].value.stringVal), (yystack_[7].value.numberVal), (yystack_[6].value.numberVal), (yystack_[2].value.integerVal));
               delete (yystack_[14].value.stringVal);
               delete (yystack_[11].value.stringVal);
               }
#line 757 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
    break;

  case 10:
#line 200 "GdfParser.yy"
                                                             {
           driver.textCbk(Text::NUMBER_OF_LAYERS, "NUMBER_OF_LAYERS", *(yystack_[1].value.quoteVal));
           delete (yystack_[1].value.quoteVal);
           }
#line 766 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
    break;

  case 11:
#line 204 "GdfParser.yy"
                                                          {
           driver.textCbk(Text::WIRE_SPACINGS, "WIRE_SPACINGS", *(yystack_[1].value.quoteVal));
           delete (yystack_[1].value.quoteVal);
           }
#line 775 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
    break;

  case 12:
#line 208 "GdfParser.yy"
                                                         {
           driver.textCbk(Text::VIA_SPACINGS, "VIA_SPACINGS", *(yystack_[1].value.quoteVal));
           delete (yystack_[1].value.quoteVal);
           }
#line 784 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
    break;

  case 13:
#line 212 "GdfParser.yy"
                                                        {
           driver.textCbk(Text::WIRE_WIDTHS, "WIRE_WIDTHS", *(yystack_[1].value.quoteVal));
           delete (yystack_[1].value.quoteVal);
           }
#line 793 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
    break;

  case 14:
#line 216 "GdfParser.yy"
                                                       {
           driver.textCbk(Text::VIA_WIDTHS, "VIA_WIDTHS", *(yystack_[1].value.quoteVal));
           delete (yystack_[1].value.quoteVal);
           }
#line 802 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
    break;

  case 15:
#line 220 "GdfParser.yy"
                                                                {
           driver.textCbk(Text::VERTICAL_WIRE_COSTS, "VERTICAL_WIRE_COSTS", *(yystack_[1].value.quoteVal));
           delete (yystack_[1].value.quoteVal);
           }
#line 811 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
    break;

  case 16:
#line 224 "GdfParser.yy"
                                                                  {
           driver.textCbk(Text::HORIZONTAL_WIRE_COSTS, "HORIZONTAL_WIRE_COSTS", *(yystack_[1].value.quoteVal));
           delete (yystack_[1].value.quoteVal);
           }
#line 820 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
    break;

  case 17:
#line 228 "GdfParser.yy"
                                                      {
           driver.textCbk(Text::VIA_COSTS, "VIA_COSTS", *(yystack_[1].value.quoteVal));
           delete (yystack_[1].value.quoteVal);
           }
#line 829 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
    break;

  case 18:
#line 232 "GdfParser.yy"
                                               {
           driver.textCbk(Text::STRING, *(yystack_[2].value.stringVal), *(yystack_[1].value.quoteVal));
           delete (yystack_[2].value.stringVal);
           delete (yystack_[1].value.quoteVal);
           }
#line 839 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
    break;

  case 19:
#line 239 "GdfParser.yy"
                                                                                                                                                  {
              driver.pathObjCbk(PathObj::SEGMENT, "", *(yystack_[15].value.stringVal), (yystack_[11].value.numberVal), (yystack_[7].value.numberVal), (yystack_[6].value.numberVal), (yystack_[2].value.numberVal), (yystack_[1].value.numberVal));
              delete (yystack_[15].value.stringVal);
              }
#line 848 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
    break;

  case 20:
#line 243 "GdfParser.yy"
                                                                                                                                              {
              driver.pathObjCbk(PathObj::VIA, "", *(yystack_[15].value.stringVal), (yystack_[11].value.numberVal), (yystack_[7].value.numberVal), (yystack_[6].value.numberVal), (yystack_[2].value.numberVal), (yystack_[1].value.numberVal));
              delete (yystack_[15].value.stringVal);
              }
#line 857 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
    break;

  case 21:
#line 247 "GdfParser.yy"
                                                                                                                                              {
              driver.pathObjCbk(PathObj::STRING, "new", *(yystack_[15].value.stringVal), (yystack_[11].value.numberVal), (yystack_[7].value.numberVal), (yystack_[6].value.numberVal), (yystack_[2].value.numberVal), (yystack_[1].value.numberVal));
              delete (yystack_[15].value.stringVal);
              }
#line 866 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
    break;

  case 24:
#line 254 "GdfParser.yy"
                                     {
           driver.pathCbk(*(yystack_[0].value.stringVal));
           delete (yystack_[0].value.stringVal);
           }
#line 875 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
    break;

  case 26:
#line 261 "GdfParser.yy"
                                                                                                  {
               driver.netPortCbk(*(yystack_[5].value.stringVal), *(yystack_[2].value.stringVal));
               delete (yystack_[5].value.stringVal);
               delete (yystack_[2].value.stringVal);
               }
#line 885 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
    break;

  case 27:
#line 266 "GdfParser.yy"
                                                                   {
               driver.netPortCbk(*(yystack_[1].value.stringVal));
               delete (yystack_[1].value.stringVal);
               }
#line 894 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
    break;

  case 30:
#line 275 "GdfParser.yy"
                                   {
          driver.netCbk(*(yystack_[0].value.stringVal));
          delete (yystack_[0].value.stringVal);
          }
#line 903 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
    break;

  case 40:
#line 295 "GdfParser.yy"
                                                     {
           driver.cellCbk(*(yystack_[2].value.stringVal));
           delete (yystack_[2].value.stringVal);
           }
#line 912 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"
    break;


#line 916 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"

            default:
              break;
            }
        }
#if YY_EXCEPTIONS
      catch (const syntax_error& yyexc)
        {
          YYCDEBUG << "Caught exception: " << yyexc.what() << '\n';
          error (yyexc);
          YYERROR;
        }
#endif // YY_EXCEPTIONS
      YY_SYMBOL_PRINT ("-> $$ =", yylhs);
      yypop_ (yylen);
      yylen = 0;
      YY_STACK_PRINT ();

      // Shift the result of the reduction.
      yypush_ (YY_NULLPTR, YY_MOVE (yylhs));
    }
    goto yynewstate;


  /*--------------------------------------.
  | yyerrlab -- here on detecting error.  |
  `--------------------------------------*/
  yyerrlab:
    // If not already recovering from an error, report this error.
    if (!yyerrstatus_)
      {
        ++yynerrs_;
        error (yyla.location, yysyntax_error_ (yystack_[0].state, yyla));
      }


    yyerror_range[1].location = yyla.location;
    if (yyerrstatus_ == 3)
      {
        /* If just tried and failed to reuse lookahead token after an
           error, discard it.  */

        // Return failure if at end of input.
        if (yyla.type_get () == yyeof_)
          YYABORT;
        else if (!yyla.empty ())
          {
            yy_destroy_ ("Error: discarding", yyla);
            yyla.clear ();
          }
      }

    // Else will try to reuse lookahead token after shifting the error token.
    goto yyerrlab1;


  /*---------------------------------------------------.
  | yyerrorlab -- error raised explicitly by YYERROR.  |
  `---------------------------------------------------*/
  yyerrorlab:
    /* Pacify compilers when the user code never invokes YYERROR and
       the label yyerrorlab therefore never appears in user code.  */
    if (false)
      YYERROR;

    /* Do not reclaim the symbols of the rule whose action triggered
       this YYERROR.  */
    yypop_ (yylen);
    yylen = 0;
    goto yyerrlab1;


  /*-------------------------------------------------------------.
  | yyerrlab1 -- common code for both syntax error and YYERROR.  |
  `-------------------------------------------------------------*/
  yyerrlab1:
    yyerrstatus_ = 3;   // Each real token shifted decrements this.
    {
      stack_symbol_type error_token;
      for (;;)
        {
          yyn = yypact_[+yystack_[0].state];
          if (!yy_pact_value_is_default_ (yyn))
            {
              yyn += yy_error_token_;
              if (0 <= yyn && yyn <= yylast_ && yycheck_[yyn] == yy_error_token_)
                {
                  yyn = yytable_[yyn];
                  if (0 < yyn)
                    break;
                }
            }

          // Pop the current state because it cannot handle the error token.
          if (yystack_.size () == 1)
            YYABORT;

          yyerror_range[1].location = yystack_[0].location;
          yy_destroy_ ("Error: popping", yystack_[0]);
          yypop_ ();
          YY_STACK_PRINT ();
        }

      yyerror_range[2].location = yyla.location;
      YYLLOC_DEFAULT (error_token.location, yyerror_range, 2);

      // Shift the error token.
      error_token.state = state_type (yyn);
      yypush_ ("Shifting", YY_MOVE (error_token));
    }
    goto yynewstate;


  /*-------------------------------------.
  | yyacceptlab -- YYACCEPT comes here.  |
  `-------------------------------------*/
  yyacceptlab:
    yyresult = 0;
    goto yyreturn;


  /*-----------------------------------.
  | yyabortlab -- YYABORT comes here.  |
  `-----------------------------------*/
  yyabortlab:
    yyresult = 1;
    goto yyreturn;


  /*-----------------------------------------------------.
  | yyreturn -- parsing is finished, return the result.  |
  `-----------------------------------------------------*/
  yyreturn:
    if (!yyla.empty ())
      yy_destroy_ ("Cleanup: discarding lookahead", yyla);

    /* Do not reclaim the symbols of the rule whose action triggered
       this YYABORT or YYACCEPT.  */
    yypop_ (yylen);
    while (1 < yystack_.size ())
      {
        yy_destroy_ ("Cleanup: popping", yystack_[0]);
        yypop_ ();
      }

    return yyresult;
  }
#if YY_EXCEPTIONS
    catch (...)
      {
        YYCDEBUG << "Exception caught: cleaning lookahead and stack\n";
        // Do not try to display the values of the reclaimed symbols,
        // as their printers might throw an exception.
        if (!yyla.empty ())
          yy_destroy_ (YY_NULLPTR, yyla);

        while (1 < yystack_.size ())
          {
            yy_destroy_ (YY_NULLPTR, yystack_[0]);
            yypop_ ();
          }
        throw;
      }
#endif // YY_EXCEPTIONS
  }

  void
  Parser::error (const syntax_error& yyexc)
  {
    error (yyexc.location, yyexc.what ());
  }

  // Generate an error message.
  std::string
  Parser::yysyntax_error_ (state_type yystate, const symbol_type& yyla) const
  {
    // Number of reported tokens (one for the "unexpected", one per
    // "expected").
    std::ptrdiff_t yycount = 0;
    // Its maximum.
    enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
    // Arguments of yyformat.
    char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];

    /* There are many possibilities here to consider:
       - If this state is a consistent state with a default action, then
         the only way this function was invoked is if the default action
         is an error action.  In that case, don't check for expected
         tokens because there are none.
       - The only way there can be no lookahead present (in yyla) is
         if this state is a consistent state with a default action.
         Thus, detecting the absence of a lookahead is sufficient to
         determine that there is no unexpected or expected token to
         report.  In that case, just report a simple "syntax error".
       - Don't assume there isn't a lookahead just because this state is
         a consistent state with a default action.  There might have
         been a previous inconsistent state, consistent state with a
         non-default action, or user semantic action that manipulated
         yyla.  (However, yyla is currently not documented for users.)
       - Of course, the expected token list depends on states to have
         correct lookahead information, and it depends on the parser not
         to perform extra reductions after fetching a lookahead from the
         scanner and before detecting a syntax error.  Thus, state merging
         (from LALR or IELR) and default reductions corrupt the expected
         token list.  However, the list is correct for canonical LR with
         one exception: it will still contain any token that will not be
         accepted due to an error action in a later state.
    */
    if (!yyla.empty ())
      {
        symbol_number_type yytoken = yyla.type_get ();
        yyarg[yycount++] = yytname_[yytoken];

        int yyn = yypact_[+yystate];
        if (!yy_pact_value_is_default_ (yyn))
          {
            /* Start YYX at -YYN if negative to avoid negative indexes in
               YYCHECK.  In other words, skip the first -YYN actions for
               this state because they are default actions.  */
            int yyxbegin = yyn < 0 ? -yyn : 0;
            // Stay within bounds of both yycheck and yytname.
            int yychecklim = yylast_ - yyn + 1;
            int yyxend = yychecklim < yyntokens_ ? yychecklim : yyntokens_;
            for (int yyx = yyxbegin; yyx < yyxend; ++yyx)
              if (yycheck_[yyx + yyn] == yyx && yyx != yy_error_token_
                  && !yy_table_value_is_error_ (yytable_[yyx + yyn]))
                {
                  if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
                    {
                      yycount = 1;
                      break;
                    }
                  else
                    yyarg[yycount++] = yytname_[yyx];
                }
          }
      }

    char const* yyformat = YY_NULLPTR;
    switch (yycount)
      {
#define YYCASE_(N, S)                         \
        case N:                               \
          yyformat = S;                       \
        break
      default: // Avoid compiler warnings.
        YYCASE_ (0, YY_("syntax error"));
        YYCASE_ (1, YY_("syntax error, unexpected %s"));
        YYCASE_ (2, YY_("syntax error, unexpected %s, expecting %s"));
        YYCASE_ (3, YY_("syntax error, unexpected %s, expecting %s or %s"));
        YYCASE_ (4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
        YYCASE_ (5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
#undef YYCASE_
      }

    std::string yyres;
    // Argument number.
    std::ptrdiff_t yyi = 0;
    for (char const* yyp = yyformat; *yyp; ++yyp)
      if (yyp[0] == '%' && yyp[1] == 's' && yyi < yycount)
        {
          yyres += yytnamerr_ (yyarg[yyi++]);
          ++yyp;
        }
      else
        yyres += *yyp;
    return yyres;
  }


  const short Parser::yypact_ninf_ = -139;

  const signed char Parser::yytable_ninf_ = -1;

  const short
  Parser::yypact_[] =
  {
     -44,    -4,    48,   -29,  -139,    39,     6,    49,    41,  -139,
     -43,    51,     9,  -139,  -139,    53,    52,    11,    14,  -139,
     -10,  -139,  -139,  -139,  -139,  -139,  -139,   -39,    15,    20,
      26,    28,    29,  -139,  -139,    55,    57,    68,     4,    71,
      32,  -139,    33,    79,    80,    81,    82,    83,    84,    85,
      86,    87,  -139,    88,    50,    65,    54,    56,    58,    59,
      60,    61,    62,    63,    64,    67,    -8,    27,  -139,   -30,
      91,  -139,  -139,  -139,  -139,  -139,  -139,  -139,  -139,  -139,
      72,  -139,    -1,    69,    70,    73,    74,    75,    76,    77,
      78,  -139,  -139,    89,    92,  -139,  -139,    90,    93,    94,
      95,    96,    97,    98,    99,   100,     0,   103,   104,   111,
     112,   113,   114,   115,   116,   117,   105,  -139,   109,   110,
     127,   129,   132,   141,   143,   144,    16,   145,   106,   107,
     108,   118,   119,   120,   121,   122,  -139,  -139,    16,   123,
     126,   128,   130,   131,   133,   134,   135,   136,   125,   137,
     140,   142,   146,   147,   148,   150,   153,   158,   138,  -139,
      16,    16,    16,    16,    16,    16,    16,    16,   155,    16,
      16,    16,    16,    16,   139,   149,   151,   101,   152,   154,
     156,   157,   159,   160,   162,   163,   164,   165,   166,   167,
     168,   169,   171,   172,   173,   170,  -139,  -139,  -139,  -139,
    -139,    16,    16,    16,  -139,    16,    16,    16,   174,   175,
     176,   179,   180,   181,   177,   178,   184,    16,    16,    16,
      16,    16,    16,   182,   183,   185,  -139,  -139,  -139
  };

  const signed char
  Parser::yydefact_[] =
  {
       0,     0,     0,     0,     1,     0,     0,     0,     0,    41,
       0,     0,     0,    43,    42,     0,     0,     0,     0,    32,
       0,    33,    35,    36,    34,    37,    38,     0,     0,     0,
       0,     0,     0,    40,    39,     0,     0,     0,     0,     0,
       0,    24,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    30,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    22,     0,
       0,    18,    10,    11,    12,    13,    14,    15,    16,    17,
       0,    28,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    25,    23,     0,     0,    31,    29,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    27,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     2,     3,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    26,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     4,     5,     6,     7,
       8,     0,     0,     0,     9,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    21,    19,    20
  };

  const short
  Parser::yypgoto_[] =
  {
    -139,  -138,  -139,  -139,  -139,    30,  -139,  -139,  -139,    18,
    -139,  -139,  -139,  -139,   124,  -139,   187,  -139,  -139
  };

  const short
  Parser::yydefgoto_[] =
  {
      -1,   138,    21,    22,    23,    68,    69,    24,    54,    81,
      82,    25,    65,     6,    26,    27,     9,    10,     2
  };

  const unsigned char
  Parser::yytable_[] =
  {
     148,     1,     8,    28,    13,     3,    20,    29,    33,    43,
      83,    84,    85,    86,    87,    67,     5,    91,    30,   136,
     137,    31,   169,   170,   171,   172,   173,   174,   175,   176,
      32,   178,   179,   180,   181,   182,    44,    45,    46,    47,
      48,    49,    50,    51,    80,   116,    95,   117,     4,     7,
      88,     8,    11,    12,    15,    16,    17,    18,    19,    20,
      40,    35,    41,   205,   206,   207,    36,   208,   209,   210,
      89,    90,    37,    42,    38,    39,    52,    53,    55,   220,
     221,   222,   223,   224,   225,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    70,    67,    93,   106,     0,    92,
      96,    71,    66,    72,   186,    73,    74,    75,    76,    77,
      78,    79,    80,    94,   128,   129,    97,    98,   118,   119,
      99,   100,   101,   102,   103,   104,   120,   121,   122,   123,
     124,   125,   130,   126,   131,   107,   105,   132,   108,   109,
     110,   111,   112,   113,   114,   115,   133,   127,   134,   135,
     139,    34,     0,   140,   141,   142,   160,     0,   161,     0,
       0,     0,   162,   163,   164,   143,   144,   145,   146,   147,
     149,   150,   158,   151,   165,   152,   153,   166,   154,   155,
     156,   157,   167,   168,   159,   177,   183,   201,   202,   203,
       0,     0,     0,   217,   218,     0,   184,    14,   185,   187,
     219,   188,     0,   189,   190,   192,   191,   193,   194,     0,
       0,   195,   196,   197,   198,   199,   200,   204,     0,     0,
       0,   211,   212,   213,   214,   215,   216,     0,     0,   226,
     227,     0,   228
  };

  const short
  Parser::yycheck_[] =
  {
     138,    45,    45,    13,    47,     9,    45,    17,    47,     5,
      18,    19,    20,    21,    22,    45,    45,    47,    28,     3,
       4,    31,   160,   161,   162,   163,   164,   165,   166,   167,
      40,   169,   170,   171,   172,   173,    32,    33,    34,    35,
      36,    37,    38,    39,    45,    45,    47,    47,     0,    10,
      23,    45,     3,    12,     3,    46,     3,     5,    47,    45,
       5,    46,     5,   201,   202,   203,    46,   205,   206,   207,
      43,    44,    46,     5,    46,    46,     5,    45,    45,   217,
     218,   219,   220,   221,   222,     6,     6,     6,     6,     6,
       6,     6,     6,     6,    29,    45,     5,     5,    -1,    69,
      82,    47,    14,    47,     3,    47,    47,    47,    47,    47,
      47,    47,    45,    41,     5,     5,    47,    47,    15,    15,
      47,    47,    47,    47,    47,    47,    15,    15,    15,    15,
      15,    15,     5,    16,     5,    45,    47,     5,    45,    45,
      45,    45,    45,    45,    45,    45,     5,    42,     5,     5,
       5,    27,    -1,    47,    47,    47,    16,    -1,    16,    -1,
      -1,    -1,    16,    16,    16,    47,    47,    47,    47,    47,
      47,    45,    47,    45,    24,    45,    45,    24,    45,    45,
      45,    45,    24,    45,    47,    30,    47,    16,    16,    16,
      -1,    -1,    -1,    16,    16,    -1,    47,    10,    47,    47,
      16,    47,    -1,    47,    47,    45,    47,    45,    45,    -1,
      -1,    47,    47,    47,    47,    47,    47,    47,    -1,    -1,
      -1,    47,    47,    47,    45,    45,    45,    -1,    -1,    47,
      47,    -1,    47
  };

  const signed char
  Parser::yystos_[] =
  {
       0,    45,    66,     9,     0,    45,    61,    10,    45,    64,
      65,     3,    12,    47,    64,     3,    46,     3,     5,    47,
      45,    50,    51,    52,    55,    59,    62,    63,    13,    17,
      28,    31,    40,    47,    62,    46,    46,    46,    46,    46,
       5,     5,     5,     5,    32,    33,    34,    35,    36,    37,
      38,    39,     5,    45,    56,    45,     6,     6,     6,     6,
       6,     6,     6,     6,     6,    60,    14,    45,    53,    54,
      29,    47,    47,    47,    47,    47,    47,    47,    47,    47,
      45,    57,    58,    18,    19,    20,    21,    22,    23,    43,
      44,    47,    53,     5,    41,    47,    57,    47,    47,    47,
      47,    47,    47,    47,    47,    47,     5,    45,    45,    45,
      45,    45,    45,    45,    45,    45,    45,    47,    15,    15,
      15,    15,    15,    15,    15,    15,    16,    42,     5,     5,
       5,     5,     5,     5,     5,     5,     3,     4,    49,     5,
      47,    47,    47,    47,    47,    47,    47,    47,    49,    47,
      45,    45,    45,    45,    45,    45,    45,    45,    47,    47,
      16,    16,    16,    16,    16,    24,    24,    24,    45,    49,
      49,    49,    49,    49,    49,    49,    49,    30,    49,    49,
      49,    49,    49,    47,    47,    47,     3,    47,    47,    47,
      47,    47,    45,    45,    45,    47,    47,    47,    47,    47,
      47,    16,    16,    16,    47,    49,    49,    49,    49,    49,
      49,    47,    47,    47,    45,    45,    45,    16,    16,    16,
      49,    49,    49,    49,    49,    49,    47,    47,    47
  };

  const signed char
  Parser::yyr1_[] =
  {
       0,    48,    49,    49,    50,    50,    50,    50,    50,    51,
      52,    52,    52,    52,    52,    52,    52,    52,    52,    53,
      53,    53,    54,    54,    56,    55,    57,    57,    58,    58,
      60,    59,    61,    62,    62,    62,    62,    62,    63,    63,
      64,    65,    65,    66
  };

  const signed char
  Parser::yyr2_[] =
  {
       0,     2,     1,     1,    18,    18,    18,    18,    18,    18,
       6,     6,     6,     6,     6,     6,     6,     6,     6,    21,
      21,    21,     1,     2,     0,     7,     8,     4,     1,     2,
       0,     7,     6,     1,     1,     1,     1,     1,     1,     2,
       6,     1,     2,     5
  };



  // YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
  // First, the terminals, then, starting at \a yyntokens_, nonterminals.
  const char*
  const Parser::yytname_[] =
  {
  "\"end of file\"", "error", "$undefined", "\"integer\"", "\"double\"",
  "\"string\"", "\"quoted chars\"", "\"path\"", "\"binary numbers\"",
  "\"GDIF\"", "\"GDIFVERSION\"", "\"COMMENT\"", "\"CELL\"", "\"PORT\"",
  "\"TYPE\"", "\"LAYER\"", "\"PT\"", "\"PATH\"", "\"IN\"", "\"OUT\"",
  "\"INOUT\"", "\"POWER\"", "\"GROUND\"", "\"NEW\"", "\"WIDTH\"",
  "\"HEIGHT\"", "\"HORIZONTAL\"", "\"VERTICAL\"", "\"INSTANCE\"",
  "\"CELLREF\"", "\"ORIENT\"", "\"TEXT\"", "\"NUMBER_OF_LAYERS\"",
  "\"WIRE_SPACINGS\"", "\"VIA_SPACINGS\"", "\"WIRE_WIDTHS\"",
  "\"VIA_WIDTHS\"", "\"VERTICAL_WIRE_COSTS\"", "\"HORIZONTAL_WIRE_COSTS\"",
  "\"VIA_COSTS\"", "\"NET\"", "\"PORTREF\"", "\"INSTREF\"", "\"SEGMENT\"",
  "\"VIA\"", "'('", "':'", "')'", "$accept", "NUMBER", "port_entry",
  "instance_entry", "text_entry", "pathobj_entry", "pathobj_entres",
  "path_entry", "$@1", "net_port_entry", "net_port_entries", "net_entry",
  "$@2", "gdif_version_entry", "cell_addon", "cell_addons", "cell_entry",
  "cell_entries", "start", YY_NULLPTR
  };

#if GDFPARSERDEBUG
  const short
  Parser::yyrline_[] =
  {
       0,   142,   142,   143,   166,   171,   176,   181,   186,   193,
     200,   204,   208,   212,   216,   220,   224,   228,   232,   239,
     243,   247,   251,   252,   254,   254,   261,   266,   271,   272,
     275,   275,   281,   284,   285,   286,   287,   288,   291,   292,
     295,   300,   301,   305
  };

  // Print the state stack on the debug stream.
  void
  Parser::yystack_print_ ()
  {
    *yycdebug_ << "Stack now";
    for (stack_type::const_iterator
           i = yystack_.begin (),
           i_end = yystack_.end ();
         i != i_end; ++i)
      *yycdebug_ << ' ' << int (i->state);
    *yycdebug_ << '\n';
  }

  // Report on the debug stream that the rule \a yyrule is going to be reduced.
  void
  Parser::yy_reduce_print_ (int yyrule)
  {
    int yylno = yyrline_[yyrule];
    int yynrhs = yyr2_[yyrule];
    // Print the symbols being reduced, and their result.
    *yycdebug_ << "Reducing stack by rule " << yyrule - 1
               << " (line " << yylno << "):\n";
    // The symbols being reduced.
    for (int yyi = 0; yyi < yynrhs; yyi++)
      YY_SYMBOL_PRINT ("   $" << yyi + 1 << " =",
                       yystack_[(yynrhs) - (yyi + 1)]);
  }
#endif // GDFPARSERDEBUG

  Parser::token_number_type
  Parser::yytranslate_ (int t)
  {
    // YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to
    // TOKEN-NUM as returned by yylex.
    static
    const token_number_type
    translate_table[] =
    {
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      45,    47,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    46,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44
    };
    const int user_token_number_max_ = 299;

    if (t <= 0)
      return yyeof_;
    else if (t <= user_token_number_max_)
      return translate_table[t];
    else
      return yy_undef_token_;
  }

} // GdfParser
#line 1482 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/gdf/bison/GdfParser.cc"

#line 310 "GdfParser.yy"
 /*** Additional Code ***/

void GdfParser::Parser::error(const Parser::location_type& l,
			    const std::string& m)
{
    driver.error(l, m);
}
