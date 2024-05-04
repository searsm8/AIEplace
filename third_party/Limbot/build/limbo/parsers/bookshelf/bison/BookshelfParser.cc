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
#define yylex   BookshelfParserlex

// First part of user prologue.
#line 4 "BookshelfParser.yy"
 /*** C/C++ Declarations ***/

#include <stdio.h>
#include <string>
#include <vector>

/*#include "expression.h"*/


#line 52 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"


#include "BookshelfParser.h"

// Second part of user prologue.
#line 155 "BookshelfParser.yy"


#include "BookshelfDriver.h"
#include "BookshelfScanner.h"

/* this "connects" the bison parser in the driver to the flex scanner class
 * object. it defines the yylex() function call to pull the next token from the
 * current lexer object of the driver context. */
#undef yylex
#define yylex driver.lexer->lex


#line 71 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"



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
#if BOOKSHELFPARSERDEBUG

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

#else // !BOOKSHELFPARSERDEBUG

# define YYCDEBUG if (false) std::cerr
# define YY_SYMBOL_PRINT(Title, Symbol)  YYUSE (Symbol)
# define YY_REDUCE_PRINT(Rule)           static_cast<void> (0)
# define YY_STACK_PRINT()                static_cast<void> (0)

#endif // !BOOKSHELFPARSERDEBUG

#define yyerrok         (yyerrstatus_ = 0)
#define yyclearin       (yyla.clear ())

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYRECOVERING()  (!!yyerrstatus_)

namespace BookshelfParser {
#line 163 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"


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
#if BOOKSHELFPARSERDEBUG
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
      case 6: // "string"
#line 145 "BookshelfParser.yy"
                    { delete (yysym.value.stringVal); }
#line 407 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
        break;

      case 7: // "quoted chars"
#line 145 "BookshelfParser.yy"
                    { delete (yysym.value.quoteVal); }
#line 413 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
        break;

      case 8: // "binary numbers"
#line 145 "BookshelfParser.yy"
                    { delete (yysym.value.binaryVal); }
#line 419 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
        break;

      case 67: // integer_array
#line 146 "BookshelfParser.yy"
                    { delete (yysym.value.integerArrayVal); }
#line 425 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
        break;

      case 68: // string_array
#line 146 "BookshelfParser.yy"
                    { delete (yysym.value.stringArrayVal); }
#line 431 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
        break;

      case 87: // orient
#line 147 "BookshelfParser.yy"
                    { delete (yysym.value.stringVal); }
#line 437 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
        break;

      case 88: // pl_status
#line 147 "BookshelfParser.yy"
                    { delete (yysym.value.stringVal); }
#line 443 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
        break;

      default:
        break;
    }
  }

#if BOOKSHELFPARSERDEBUG
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

#if BOOKSHELFPARSERDEBUG
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
#endif // BOOKSHELFPARSERDEBUG

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
#line 41 "BookshelfParser.yy"
{
    // initialize the initial location object
    yyla.location.begin.filename = yyla.location.end.filename = &driver.streamname;
}

#line 588 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"


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
#line 172 "BookshelfParser.yy"
                        {
				(yylhs.value.integerArrayVal) = new IntegerArray(1, (yystack_[0].value.integerVal));
			  }
#line 717 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 3:
#line 175 "BookshelfParser.yy"
                                                  {
				(yystack_[1].value.integerArrayVal)->push_back((yystack_[0].value.integerVal));
				(yylhs.value.integerArrayVal) = (yystack_[1].value.integerArrayVal);
			  }
#line 726 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 4:
#line 180 "BookshelfParser.yy"
                      {
				(yylhs.value.stringArrayVal) = new StringArray(1, *(yystack_[0].value.stringVal));
                delete (yystack_[0].value.stringVal);
			  }
#line 735 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 5:
#line 184 "BookshelfParser.yy"
                                                {
				(yystack_[1].value.stringArrayVal)->push_back(*(yystack_[0].value.stringVal));
                delete (yystack_[0].value.stringVal);
				(yylhs.value.stringArrayVal) = (yystack_[1].value.stringArrayVal);
			  }
#line 745 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 6:
#line 190 "BookshelfParser.yy"
                 {(yylhs.value.numberVal) = (yystack_[0].value.integerVal);}
#line 751 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 7:
#line 191 "BookshelfParser.yy"
                {(yylhs.value.numberVal) = (yystack_[0].value.doubleVal);}
#line 757 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 11:
#line 201 "BookshelfParser.yy"
                                          {
              driver.numNodeTerminalsCbk((yystack_[4].value.integerVal), (yystack_[0].value.integerVal));
             }
#line 765 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 12:
#line 205 "BookshelfParser.yy"
                                          {
              driver.numNodeTerminalsCbk((yystack_[3].value.integerVal), (yystack_[0].value.integerVal));
             }
#line 773 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 13:
#line 209 "BookshelfParser.yy"
                                      {
              driver.numNodeTerminalsCbk((yystack_[4].value.integerVal), (yystack_[0].value.integerVal));
             }
#line 781 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 14:
#line 213 "BookshelfParser.yy"
                                      {
              driver.numNodeTerminalsCbk((yystack_[3].value.integerVal), (yystack_[0].value.integerVal));
             }
#line 789 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 18:
#line 223 "BookshelfParser.yy"
                                                           {
                 driver.terminalEntryCbk(*(yystack_[4].value.stringVal), (yystack_[3].value.integerVal), (yystack_[2].value.integerVal));
                     delete (yystack_[4].value.stringVal);
                     }
#line 798 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 19:
#line 227 "BookshelfParser.yy"
                                                                  {
                     driver.terminalNIEntryCbk(*(yystack_[4].value.stringVal), (yystack_[3].value.integerVal), (yystack_[2].value.integerVal));
                     delete (yystack_[4].value.stringVal);
                     }
#line 807 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 20:
#line 231 "BookshelfParser.yy"
                                                         {
                 driver.nodeEntryCbk(*(yystack_[4].value.stringVal), (yystack_[3].value.integerVal), (yystack_[2].value.integerVal), *(yystack_[1].value.stringVal));
                 delete (yystack_[4].value.stringVal);
                 delete (yystack_[1].value.stringVal);
                 }
#line 817 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 21:
#line 236 "BookshelfParser.yy"
                                              {
                 driver.nodeEntryCbk(*(yystack_[3].value.stringVal), (yystack_[2].value.integerVal), (yystack_[1].value.integerVal));
                 delete (yystack_[3].value.stringVal);
                 }
#line 826 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 28:
#line 259 "BookshelfParser.yy"
                                      {driver.numNetCbk((yystack_[0].value.integerVal));}
#line 832 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 29:
#line 260 "BookshelfParser.yy"
                                      {driver.numPinCbk((yystack_[0].value.integerVal));}
#line 838 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 33:
#line 268 "BookshelfParser.yy"
                        {(yylhs.value.charVal)='O';}
#line 844 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 34:
#line 269 "BookshelfParser.yy"
                        {(yylhs.value.charVal)='I';}
#line 850 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 35:
#line 270 "BookshelfParser.yy"
                        {(yylhs.value.charVal)='B';}
#line 856 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 36:
#line 273 "BookshelfParser.yy"
                                                                                       {
               driver.netPinEntryCbk(*(yystack_[9].value.stringVal), 'O', (yystack_[6].value.numberVal), (yystack_[5].value.numberVal), (yystack_[3].value.numberVal), (yystack_[2].value.numberVal), *(yystack_[1].value.stringVal));
               delete (yystack_[9].value.stringVal);
               delete (yystack_[1].value.stringVal);
               }
#line 866 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 37:
#line 278 "BookshelfParser.yy"
                                                                                {
               driver.netPinEntryCbk(*(yystack_[8].value.stringVal), (yystack_[7].value.charVal), (yystack_[5].value.numberVal), (yystack_[4].value.numberVal), (yystack_[2].value.numberVal), (yystack_[1].value.numberVal));
               delete (yystack_[8].value.stringVal);
               }
#line 875 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 38:
#line 282 "BookshelfParser.yy"
                                                             {
               driver.netPinEntryCbk(*(yystack_[5].value.stringVal), (yystack_[4].value.charVal), (yystack_[2].value.numberVal), (yystack_[1].value.numberVal));
               delete (yystack_[5].value.stringVal);
               }
#line 884 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 42:
#line 293 "BookshelfParser.yy"
                                              {driver.netNameAndDegreeCbk(*(yystack_[0].value.stringVal), (yystack_[1].value.integerVal)); delete (yystack_[0].value.stringVal);}
#line 890 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 44:
#line 298 "BookshelfParser.yy"
                            {driver.netEntryCbk();}
#line 896 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 51:
#line 317 "BookshelfParser.yy"
               {(yylhs.value.stringVal) = new std::string ("N");}
#line 902 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 52:
#line 318 "BookshelfParser.yy"
                  {(yylhs.value.stringVal) = new std::string ("S");}
#line 908 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 53:
#line 319 "BookshelfParser.yy"
                  {(yylhs.value.stringVal) = new std::string ("W");}
#line 914 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 54:
#line 320 "BookshelfParser.yy"
                  {(yylhs.value.stringVal) = new std::string ("E");}
#line 920 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 55:
#line 321 "BookshelfParser.yy"
                   {(yylhs.value.stringVal) = new std::string ("FN");}
#line 926 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 56:
#line 322 "BookshelfParser.yy"
                   {(yylhs.value.stringVal) = new std::string ("FS");}
#line 932 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 57:
#line 323 "BookshelfParser.yy"
                   {(yylhs.value.stringVal) = new std::string ("FW");}
#line 938 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 58:
#line 324 "BookshelfParser.yy"
                   {(yylhs.value.stringVal) = new std::string ("FE");}
#line 944 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 59:
#line 327 "BookshelfParser.yy"
                      {(yylhs.value.stringVal) = new std::string("FIXED");}
#line 950 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 60:
#line 328 "BookshelfParser.yy"
                         {(yylhs.value.stringVal) = new std::string("FIXED_NI");}
#line 956 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 61:
#line 329 "BookshelfParser.yy"
                       {(yylhs.value.stringVal) = new std::string("PLACED");}
#line 962 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 62:
#line 330 "BookshelfParser.yy"
                         {(yylhs.value.stringVal) = new std::string("UNPLACED");}
#line 968 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 63:
#line 333 "BookshelfParser.yy"
                                                          {
              driver.plNodeEntryCbk(*(yystack_[5].value.stringVal), (yystack_[4].value.numberVal), (yystack_[3].value.numberVal), *(yystack_[1].value.stringVal), *(yystack_[0].value.stringVal));
              delete (yystack_[5].value.stringVal);
              delete (yystack_[1].value.stringVal);
              delete (yystack_[0].value.stringVal);
              }
#line 979 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 64:
#line 339 "BookshelfParser.yy"
                                                {
              driver.plNodeEntryCbk(*(yystack_[4].value.stringVal), (yystack_[3].value.numberVal), (yystack_[2].value.numberVal), *(yystack_[0].value.stringVal));
              delete (yystack_[4].value.stringVal);
              delete (yystack_[0].value.stringVal);
              }
#line 989 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 72:
#line 363 "BookshelfParser.yy"
                                      {driver.sclNumRows((yystack_[0].value.integerVal));}
#line 995 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 74:
#line 367 "BookshelfParser.yy"
                                               {
                  driver.sclCoreRowStart("HORIZONTAL");
                  }
#line 1003 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 75:
#line 370 "BookshelfParser.yy"
                                             {
                  driver.sclCoreRowStart("VERTICAL");
                  }
#line 1011 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 77:
#line 376 "BookshelfParser.yy"
                                                  {driver.sclCoreRowCoordinate((yystack_[0].value.integerVal));}
#line 1017 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 78:
#line 377 "BookshelfParser.yy"
                                           {driver.sclCoreRowHeight((yystack_[0].value.integerVal));}
#line 1023 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 79:
#line 378 "BookshelfParser.yy"
                                              {driver.sclCoreRowSitewidth((yystack_[0].value.integerVal));}
#line 1029 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 80:
#line 379 "BookshelfParser.yy"
                                                {driver.sclCoreRowSitespacing((yystack_[0].value.integerVal));}
#line 1035 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 81:
#line 380 "BookshelfParser.yy"
                                              {driver.sclCoreRowSiteorient(*(yystack_[0].value.stringVal)); delete (yystack_[0].value.stringVal);}
#line 1041 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 82:
#line 381 "BookshelfParser.yy"
                                               {driver.sclCoreRowSiteorient((yystack_[0].value.integerVal));}
#line 1047 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 83:
#line 382 "BookshelfParser.yy"
                                                {driver.sclCoreRowSitesymmetry(*(yystack_[0].value.stringVal)); delete (yystack_[0].value.stringVal);}
#line 1053 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 84:
#line 383 "BookshelfParser.yy"
                                                 {driver.sclCoreRowSitesymmetry((yystack_[0].value.integerVal));}
#line 1059 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 85:
#line 384 "BookshelfParser.yy"
                                                 {driver.sclCoreRowSubRowOrigin((yystack_[0].value.integerVal));}
#line 1065 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 86:
#line 385 "BookshelfParser.yy"
                                             {driver.sclCoreRowNumSites((yystack_[0].value.integerVal));}
#line 1071 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 90:
#line 395 "BookshelfParser.yy"
                          {
                  driver.sclCoreRowEnd();
                  }
#line 1079 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 98:
#line 417 "BookshelfParser.yy"
                          {
          driver.wtsNetWeightEntry(*(yystack_[1].value.stringVal), (yystack_[0].value.numberVal));
          delete (yystack_[1].value.stringVal);
          }
#line 1088 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 107:
#line 440 "BookshelfParser.yy"
                                                       {
              driver.shapesNumNonRectangularNodesCbk((yystack_[0].value.integerVal));
             }
#line 1096 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 109:
#line 446 "BookshelfParser.yy"
                                                           {
             driver.shapesEntryCbk(*(yystack_[4].value.stringVal), (yystack_[3].value.numberVal), (yystack_[2].value.numberVal), (yystack_[1].value.numberVal), (yystack_[0].value.numberVal));
             delete (yystack_[4].value.stringVal); 
             }
#line 1105 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 110:
#line 450 "BookshelfParser.yy"
                                  {
                 driver.shapesNodeNameCbk(*(yystack_[2].value.stringVal), (yystack_[0].value.integerVal));
             }
#line 1113 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 114:
#line 463 "BookshelfParser.yy"
                                   {
               driver.shapesEndCbk();
               }
#line 1121 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 119:
#line 475 "BookshelfParser.yy"
                                                  {
           driver.routeGridCbk((yystack_[2].value.integerVal), (yystack_[1].value.integerVal), (yystack_[0].value.integerVal)); 
           }
#line 1129 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 121:
#line 481 "BookshelfParser.yy"
                                                                 {
                        driver.routeVerticalCapacityCbk(*(yystack_[0].value.integerArrayVal)); 
                        delete (yystack_[0].value.integerArrayVal);
                        }
#line 1138 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 123:
#line 488 "BookshelfParser.yy"
                                                                     {
                          driver.routeHorizontalCapacityCbk(*(yystack_[0].value.integerArrayVal)); 
                          delete (yystack_[0].value.integerArrayVal);
                          }
#line 1147 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 125:
#line 495 "BookshelfParser.yy"
                                                          {
                     driver.routeMinWireWidthCbk(*(yystack_[0].value.integerArrayVal));
                     delete (yystack_[0].value.integerArrayVal);
                     }
#line 1156 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 127:
#line 502 "BookshelfParser.yy"
                                                              {
                     driver.routeMinWireSpacingCbk(*(yystack_[0].value.integerArrayVal));
                     delete (yystack_[0].value.integerArrayVal);
                     }
#line 1165 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 129:
#line 509 "BookshelfParser.yy"
                                                     {
                  driver.routeViaSpacingCbk(*(yystack_[0].value.integerArrayVal));
                  delete (yystack_[0].value.integerArrayVal);
                  }
#line 1174 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 131:
#line 516 "BookshelfParser.yy"
                                                     {
                  driver.routeGridOriginCbk((yystack_[1].value.numberVal), (yystack_[0].value.numberVal));
                  }
#line 1182 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 133:
#line 522 "BookshelfParser.yy"
                                                 {
                driver.routeTileSizeCbk((yystack_[1].value.numberVal), (yystack_[0].value.numberVal));
                }
#line 1190 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 135:
#line 528 "BookshelfParser.yy"
                                                            {
                        driver.routeBlockagePorosityCbk((yystack_[0].value.integerVal));
                        }
#line 1198 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 137:
#line 534 "BookshelfParser.yy"
                                                        {
                      driver.routeNumNiTerminalsCbk((yystack_[0].value.integerVal));
                      }
#line 1206 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 139:
#line 540 "BookshelfParser.yy"
                                 {
                driver.routePinLayerCbk(*(yystack_[1].value.stringVal), (yystack_[0].value.integerVal));
                delete (yystack_[1].value.stringVal);
                }
#line 1215 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 140:
#line 544 "BookshelfParser.yy"
                                {
                driver.routePinLayerCbk(*(yystack_[1].value.stringVal), *(yystack_[0].value.stringVal));
                delete (yystack_[1].value.stringVal);
                delete (yystack_[0].value.stringVal);
                }
#line 1225 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 145:
#line 559 "BookshelfParser.yy"
                                                            {
                         driver.routeNumBlockageNodes((yystack_[0].value.integerVal));
                         }
#line 1233 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 147:
#line 565 "BookshelfParser.yy"
                                                         {
                          driver.routeBlockageNodeLayerCbk(*(yystack_[2].value.stringVal), (yystack_[1].value.integerVal), *(yystack_[0].value.integerArrayVal));
                          delete (yystack_[2].value.stringVal); 
                          delete (yystack_[0].value.integerArrayVal);
                          }
#line 1243 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 148:
#line 570 "BookshelfParser.yy"
                                                        {
                          driver.routeBlockageNodeLayerCbk(*(yystack_[2].value.stringVal), (yystack_[1].value.integerVal), *(yystack_[0].value.stringArrayVal));
                          delete (yystack_[2].value.stringVal); 
                          delete (yystack_[0].value.stringArrayVal);
                          }
#line 1253 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;

  case 166:
#line 606 "BookshelfParser.yy"
                                    {
              driver.auxCbk(*(yystack_[2].value.stringVal), *(yystack_[0].value.stringArrayVal));
              delete (yystack_[2].value.stringVal);
              delete (yystack_[0].value.stringArrayVal);
              }
#line 1263 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"
    break;


#line 1267 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"

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
       7,     0,   -50,    74,    12,    41,   108,   139,   151,   152,
      66,  -139,    11,  -139,    13,  -139,     8,  -139,    56,  -139,
      37,  -139,    58,    39,    62,  -139,    81,    62,   135,   153,
     154,   155,   156,   157,   158,   159,  -139,  -139,  -139,  -139,
    -139,  -139,  -139,   100,   103,  -139,    60,  -139,   104,   105,
    -139,    15,  -139,    71,  -139,   123,  -139,   106,    34,  -139,
      71,   163,   161,  -139,   107,   124,  -139,   109,   110,   111,
     112,   113,   114,   115,   116,   117,   118,   119,   170,   183,
     184,   185,   186,   187,   188,   189,   190,   125,  -139,   129,
    -139,  -139,  -139,  -139,  -139,   191,  -139,  -139,  -139,  -139,
    -139,  -139,  -139,   192,   194,  -139,   195,  -139,  -139,   130,
     196,   198,  -139,   140,  -139,   131,  -139,   164,  -139,  -139,
      71,  -139,  -139,   199,  -139,   138,    67,   201,   180,  -139,
    -139,   163,   202,  -139,     3,   204,   203,   206,   207,   207,
     207,   207,   207,    71,    71,   208,   209,   210,  -139,  -139,
    -139,  -139,  -139,  -139,  -139,  -139,  -139,  -139,    76,   205,
     211,  -139,   212,   215,   213,  -139,    61,   101,   216,  -139,
    -139,  -139,  -139,   218,  -139,   102,  -139,   136,  -139,   150,
    -139,  -139,  -139,  -139,   160,   162,   166,   167,   168,   169,
     171,   172,  -139,     9,  -139,   201,  -139,   219,    71,  -139,
     204,   220,  -139,   222,   222,   222,   222,   222,    71,    71,
    -139,  -139,  -139,  -139,  -139,  -139,   205,   146,  -139,   215,
     217,   173,   224,   174,    20,   234,  -139,  -139,  -139,   178,
    -139,  -139,    68,   240,   241,   242,   243,     5,   147,   244,
     245,  -139,  -139,  -139,  -139,    71,   246,  -139,  -139,  -139,
     222,   191,   193,   247,   197,   248,  -139,   225,   250,   251,
    -139,    71,  -139,  -139,  -139,  -139,  -139,  -139,  -139,  -139,
      83,  -139,  -139,  -139,  -139,  -139,  -139,  -139,  -139,  -139,
    -139,    71,  -139,   252,  -139,   253,  -139,  -139,  -139,  -139,
      71,  -139,  -139,  -139,  -139,  -139,  -139,  -139,  -139,     2,
    -139,    71,    71,   137,  -139,   256,  -139
  };

  const unsigned char
  Parser::yydefact_[] =
  {
     170,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   171,     0,   172,     0,   173,     0,   174,   103,   175,
     115,   176,   162,   177,   168,   178,     0,   169,     0,     0,
       0,     0,     0,     0,     0,     0,    70,     9,    49,    96,
     105,   117,    10,     0,     0,    15,     0,    27,     0,     0,
      30,     0,    50,     0,    65,    68,    71,     0,     0,    97,
       0,   100,   102,   106,     0,     0,   118,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   153,   154,
     155,   156,   157,   158,   159,   160,   161,     0,   164,     0,
     165,   163,   167,     1,     4,   166,    69,     8,    26,    48,
      95,   104,   116,     0,     0,    17,     0,    16,    22,    25,
       0,     0,    32,     0,    31,     0,    45,    47,     6,     7,
       0,    67,    66,     0,    73,     0,     0,    92,    94,    98,
      99,   101,     0,   108,     0,   112,   114,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   120,   122,
     124,   126,   128,   130,   132,   134,   136,   138,     0,   142,
     144,   146,     0,   150,   152,     5,     0,     0,     0,    24,
      23,    28,    29,     0,    43,     0,    39,    44,    46,     0,
      72,    74,    75,    76,     0,     0,     0,     0,     0,     0,
       0,     0,    87,     0,    91,    93,   107,     0,     0,   111,
     113,     0,     2,   121,   123,   125,   127,   129,     0,     0,
     135,   137,   145,   139,   140,   141,   143,     0,   149,   151,
       0,     0,     0,     0,     0,     0,    33,    34,    35,     0,
      41,    40,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    89,    90,    88,   110,     0,     0,     3,   131,   133,
     147,   148,     0,     0,     0,     0,    21,     0,     0,     0,
      42,     0,    51,    52,    53,    54,    55,    56,    57,    58,
      64,    77,    78,    79,    80,    82,    81,    84,    83,    85,
      86,     0,   119,     0,    12,     0,    14,    20,    18,    19,
       0,    59,    60,    61,    62,    63,   109,    11,    13,     0,
      38,     0,     0,     0,    37,     0,    36
  };

  const short
  Parser::yypgoto_[] =
  {
    -139,  -138,    38,   -60,  -139,   214,  -139,   165,  -139,  -139,
    -139,   221,  -139,  -139,    84,  -139,  -139,   148,  -139,  -139,
    -139,    26,  -139,   223,  -139,  -139,  -139,  -139,  -139,    73,
    -139,   141,  -139,  -139,  -139,   226,  -139,  -139,  -139,  -139,
     128,  -139,  -139,  -139,  -139,  -139,  -139,  -139,  -139,  -139,
    -139,  -139,  -139,  -139,   120,  -139,  -139,  -139,   121,  -139,
    -139,  -139,  -139,   266,  -139,  -139
  };

  const short
  Parser::yydefgoto_[] =
  {
      -1,   203,    95,   120,    10,    45,    46,   108,   109,    11,
      12,    50,    51,   229,   176,   177,   115,   116,   117,    13,
      14,   270,   295,    54,    55,    15,    16,    58,   126,   192,
     193,   127,   128,    17,    18,    61,    62,    19,    20,    65,
     135,   136,    21,    22,    78,    79,    80,    81,    82,    83,
      84,    85,    86,    87,   159,   160,    88,    89,   163,   164,
      90,    91,    23,    24,    25,    26
  };

  const short
  Parser::yytable_[] =
  {
     129,   204,   205,   206,   207,   300,     2,   118,   119,   275,
       1,    56,   241,     2,    47,    28,    52,    36,   112,    53,
      48,    49,    57,   256,    48,    49,   257,   184,   185,   186,
     187,   188,   189,   190,   191,   242,     3,   124,     4,     5,
      63,     6,     7,     8,     9,   113,    37,   258,   259,   125,
      64,   262,   263,   264,   265,   266,   267,   268,   269,    59,
     179,    66,    60,   105,   220,    92,   106,   301,   197,    42,
     183,    43,    44,   221,   198,   118,   119,    43,    44,   250,
     213,    93,   214,   208,   209,   184,   185,   186,   187,   188,
     189,   190,   191,    67,    68,    69,    70,    71,    72,    73,
      74,    75,    76,    77,   222,    29,    30,    31,    32,    33,
      34,    35,   223,    38,   262,   263,   264,   265,   266,   267,
     268,   269,   291,   292,   293,   294,   121,   133,   157,    53,
     134,   158,   161,   169,   174,   162,   106,   175,   245,   230,
     304,    94,   175,   305,    39,   226,   227,   228,   248,   249,
     202,   277,    94,   278,   181,   182,    40,    41,    96,    97,
      98,    99,   100,   101,   102,   103,   130,    60,   104,   110,
     111,   123,   132,   148,   137,   138,   139,   140,   141,   142,
     143,   144,   145,   146,   147,   281,   149,   150,   151,   152,
     153,   154,   155,   156,   113,   125,   166,   165,   167,   168,
     171,   290,   172,   180,   194,   173,   196,   199,   215,   134,
     201,   202,   210,   211,   212,   232,   217,   158,   218,   162,
     224,   296,   225,   244,   246,   233,   247,   234,   287,   252,
     299,   235,   236,   237,   238,   254,   239,   240,   253,   255,
     260,   302,   303,   261,   271,   272,   273,   274,   279,   280,
     282,   284,   286,   288,   289,   251,   297,   298,   283,   306,
     107,   231,   285,   276,   200,   178,   243,    27,     0,   195,
       0,     0,   114,     0,   170,     0,     0,     0,   122,     0,
     216,     0,     0,     0,     0,   219,     0,     0,   131
  };

  const short
  Parser::yycheck_[] =
  {
      60,   139,   140,   141,   142,     3,     6,     4,     5,     4,
       3,     3,     3,     6,     3,    65,     3,     5,     3,     6,
       9,    10,    14,     3,     9,    10,     6,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    29,     3,    31,    32,
       3,    34,    35,    36,    37,    30,     5,    27,    28,    15,
      13,    46,    47,    48,    49,    50,    51,    52,    53,     3,
     120,     3,     6,     3,     3,     3,     6,    65,    65,     3,
       3,    11,    12,    12,   134,     4,     5,    11,    12,   217,
       4,     0,     6,   143,   144,    18,    19,    20,    21,    22,
      23,    24,    25,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,     3,    31,    32,    33,    34,    35,
      36,    37,    11,     5,    46,    47,    48,    49,    50,    51,
      52,    53,    39,    40,    41,    42,     3,     3,     3,     6,
       6,     6,     3,     3,     3,     6,     6,     6,   198,     3,
       3,     6,     6,     6,     5,    43,    44,    45,   208,   209,
       4,     4,     6,     6,    16,    17,     5,     5,     5,     5,
       5,     5,     5,     5,     5,    65,     3,     6,    65,    65,
      65,    65,    65,     3,    65,    65,    65,    65,    65,    65,
      65,    65,    65,    65,    65,   245,     3,     3,     3,     3,
       3,     3,     3,     3,    30,    15,     4,     6,     4,     4,
       4,   261,     4,     4,     3,    65,     4,     3,     3,     6,
       4,     4,     4,     4,     4,    65,     4,     6,     3,     6,
       4,   281,     4,     4,     4,    65,     4,    65,     3,    12,
     290,    65,    65,    65,    65,    11,    65,    65,    65,    65,
       6,   301,   302,    65,     4,     4,     4,     4,     4,     4,
       4,     4,     4,     3,     3,   217,     4,     4,    65,     3,
      46,   177,    65,   237,   136,   117,   193,     1,    -1,   128,
      -1,    -1,    51,    -1,   109,    -1,    -1,    -1,    55,    -1,
     160,    -1,    -1,    -1,    -1,   164,    -1,    -1,    62
  };

  const unsigned char
  Parser::yystos_[] =
  {
       0,     3,     6,    29,    31,    32,    34,    35,    36,    37,
      70,    75,    76,    85,    86,    91,    92,    99,   100,   103,
     104,   108,   109,   128,   129,   130,   131,   129,    65,    31,
      32,    33,    34,    35,    36,    37,     5,     5,     5,     5,
       5,     5,     3,    11,    12,    71,    72,     3,     9,    10,
      77,    78,     3,     6,    89,    90,     3,    14,    93,     3,
       6,   101,   102,     3,    13,   105,     3,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,   110,   111,
     112,   113,   114,   115,   116,   117,   118,   119,   122,   123,
     126,   127,     3,     0,     6,    68,     5,     5,     5,     5,
       5,     5,     5,    65,    65,     3,     6,    71,    73,    74,
      65,    65,     3,    30,    77,    82,    83,    84,     4,     5,
      69,     3,    89,    65,     3,    15,    94,    97,    98,    69,
       3,   101,    65,     3,     6,   106,   107,    65,    65,    65,
      65,    65,    65,    65,    65,    65,    65,    65,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     6,   120,
     121,     3,     6,   124,   125,     6,     4,     4,     4,     3,
      73,     4,     4,    65,     3,     6,    80,    81,    83,    69,
       4,    16,    17,     3,    18,    19,    20,    21,    22,    23,
      24,    25,    95,    96,     3,    97,     4,    65,    69,     3,
     106,     4,     4,    67,    67,    67,    67,    67,    69,    69,
       4,     4,     4,     4,     6,     3,   120,     4,     3,   124,
       3,    12,     3,    11,     4,     4,    43,    44,    45,    79,
       3,    80,    65,    65,    65,    65,    65,    65,    65,    65,
      65,     3,    26,    95,     4,    69,     4,     4,    69,    69,
      67,    68,    12,    65,    11,    65,     3,     6,    27,    28,
       6,    65,    46,    47,    48,    49,    50,    51,    52,    53,
      87,     4,     4,     4,     4,     4,    87,     4,     6,     4,
       4,    69,     4,    65,     4,    65,     4,     3,     3,     3,
      69,    39,    40,    41,    42,    88,    69,     4,     4,    69,
       3,    65,    69,    69,     3,     6,     3
  };

  const unsigned char
  Parser::yyr1_[] =
  {
       0,    66,    67,    67,    68,    68,    69,    69,    70,    70,
      70,    71,    71,    71,    71,    72,    72,    72,    73,    73,
      73,    73,    74,    74,    74,    75,    76,    76,    77,    77,
      78,    78,    78,    79,    79,    79,    80,    80,    80,    81,
      81,    81,    82,    82,    83,    84,    84,    85,    86,    86,
      86,    87,    87,    87,    87,    87,    87,    87,    87,    88,
      88,    88,    88,    89,    89,    90,    90,    90,    91,    92,
      92,    92,    93,    93,    94,    94,    94,    95,    95,    95,
      95,    95,    95,    95,    95,    95,    95,    96,    96,    96,
      97,    97,    98,    98,    99,   100,   100,   100,   101,   101,
     102,   102,   103,   103,   104,   104,   104,   105,   105,   106,
     106,   106,   107,   107,   108,   108,   109,   109,   109,   110,
     110,   111,   111,   112,   112,   113,   113,   114,   114,   115,
     115,   116,   116,   117,   117,   118,   118,   119,   119,   120,
     120,   120,   121,   121,   122,   123,   123,   124,   124,   124,
     125,   125,   126,   127,   127,   127,   127,   127,   127,   127,
     127,   127,   128,   128,   128,   128,   129,   129,   130,   130,
     131,   131,   131,   131,   131,   131,   131,   131,   131
  };

  const signed char
  Parser::yyr2_[] =
  {
       0,     2,     1,     2,     1,     2,     1,     1,     3,     2,
       2,     7,     6,     7,     6,     1,     2,     2,     5,     5,
       5,     4,     1,     2,     2,     3,     3,     2,     3,     3,
       1,     2,     2,     1,     1,     1,    10,     9,     6,     1,
       2,     2,     4,     2,     2,     1,     2,     3,     3,     2,
       2,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     6,     5,     1,     2,     2,     2,     3,
       2,     2,     3,     2,     2,     2,     2,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     1,     2,     2,
       3,     2,     1,     2,     3,     3,     2,     2,     2,     2,
       1,     2,     2,     1,     3,     2,     2,     3,     2,     5,
       3,     2,     1,     2,     3,     1,     3,     2,     2,     5,
       2,     3,     2,     3,     2,     3,     2,     3,     2,     3,
       2,     4,     2,     4,     2,     3,     2,     3,     2,     2,
       2,     2,     1,     2,     2,     3,     2,     3,     3,     2,
       1,     2,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     2,     2,     2,     3,     2,     1,     2,
       0,     1,     1,     1,     1,     1,     1,     1,     1
  };



  // YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
  // First, the terminals, then, starting at \a yyntokens_, nonterminals.
  const char*
  const Parser::yytname_[] =
  {
  "\"end of file\"", "error", "$undefined", "\"end of line\"",
  "\"integer\"", "\"double\"", "\"string\"", "\"quoted chars\"",
  "\"binary numbers\"", "\"NumNets\"", "\"NumPins\"", "\"NumNodes\"",
  "\"NumTerminals\"", "\"NumNonRectangularNodes\"", "\"NumRows\"",
  "\"CoreRow\"", "\"Horizontal\"", "\"Vertical\"", "\"Coordinate\"",
  "\"Height\"", "\"Sitewidth\"", "\"Sitespacing\"", "\"Siteorient\"",
  "\"Sitesymmetry\"", "\"SubrowOrigin\"", "\"NumSites\"", "\"End\"",
  "\"terminal\"", "\"terminal_NI\"", "\"UCLA\"", "\"NetDegree\"",
  "\"scl\"", "\"nodes\"", "\"nets\"", "\"pl\"", "\"wts\"", "\"shapes\"",
  "\"route\"", "\"aux\"", "\"FIXED\"", "\"FIXED_NI\"", "\"PLACED\"",
  "\"UNPLACED\"", "\"O\"", "\"I\"", "\"B\"", "\"N\"", "\"S\"", "\"W\"",
  "\"E\"", "\"FN\"", "\"FS\"", "\"FW\"", "\"FE\"", "\"Grid\"",
  "\"VerticalCapacity\"", "\"HorizontalCapacity\"", "\"MinWireWidth\"",
  "\"MinWireSpacing\"", "\"ViaSpacing\"", "\"GridOrigin\"", "\"TileSize\"",
  "\"BlockagePorosity\"", "\"NumNiTerminals\"", "\"NumBlockageNodes\"",
  "':'", "$accept", "integer_array", "string_array", "NUMBER",
  "nodes_header", "nodes_number", "nodes_numbers", "nodes_node_entry",
  "nodes_block_node_entries", "bookshelf_nodes", "nets_header",
  "nets_number", "nets_numbers", "nets_pin_direct", "nets_pin_entry",
  "nets_pin_entries", "nets_name", "nets_entry", "nets_entries",
  "bookshelf_nets", "pl_header", "orient", "pl_status", "pl_node_entry",
  "pl_node_entries", "bookshelf_pl", "scl_header", "scl_numbers",
  "scl_corerow_start", "scl_corerow_property", "scl_corerow_properties",
  "scl_corerow_entry", "scl_corerow_entries", "bookshelf_scl",
  "wts_header", "wts_entry", "wts_entries", "bookshelf_wts",
  "shapes_header", "shapes_number", "shapes_one_line_entry",
  "shapes_node_entries", "bookshelf_shapes", "route_header", "grid_entry",
  "vertical_capacity_entry", "horizontal_capacity_entry",
  "min_wire_width_entry", "min_wire_spacing_entry", "via_spacing_entry",
  "grid_origin_entry", "tile_size_entry", "blockage_porosity_entry",
  "num_ni_terminals_entry", "pin_layer_entry", "pin_layer_entries",
  "pin_layer_block", "num_blockage_nodes_entry",
  "blockage_node_layer_entry", "blockage_node_layer_entries",
  "blockage_node_layer_block", "route_info_block", "bookshelf_route",
  "aux_entry", "bookshelf_aux", "start", YY_NULLPTR
  };

#if BOOKSHELFPARSERDEBUG
  const short
  Parser::yyrline_[] =
  {
       0,   172,   172,   175,   180,   184,   190,   191,   195,   196,
     197,   200,   204,   208,   212,   218,   219,   220,   223,   227,
     231,   236,   242,   243,   244,   248,   255,   256,   259,   260,
     263,   264,   265,   268,   269,   270,   273,   278,   282,   288,
     289,   290,   293,   294,   297,   301,   302,   306,   312,   313,
     314,   317,   318,   319,   320,   321,   322,   323,   324,   327,
     328,   329,   330,   333,   339,   346,   347,   348,   352,   358,
     359,   360,   363,   364,   367,   370,   373,   376,   377,   378,
     379,   380,   381,   382,   383,   384,   385,   388,   389,   390,
     393,   398,   401,   402,   406,   412,   413,   414,   417,   421,
     424,   425,   429,   431,   435,   436,   437,   440,   443,   446,
     450,   453,   456,   457,   461,   466,   470,   471,   472,   475,
     478,   481,   485,   488,   492,   495,   499,   502,   506,   509,
     513,   516,   519,   522,   525,   528,   531,   534,   537,   540,
     544,   549,   552,   553,   556,   559,   562,   565,   570,   575,
     578,   579,   582,   585,   586,   587,   588,   589,   590,   591,
     592,   593,   597,   598,   599,   600,   606,   611,   614,   615,
     620,   621,   622,   623,   624,   625,   626,   627,   628
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
#endif // BOOKSHELFPARSERDEBUG

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
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    65,     2,
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
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64
    };
    const int user_token_number_max_ = 319;

    if (t <= 0)
      return yyeof_;
    else if (t <= user_token_number_max_)
      return translate_table[t];
    else
      return yy_undef_token_;
  }

} // BookshelfParser
#line 1938 "/home/msears/AIEplace/cpp/Limbo/build/limbo/parsers/bookshelf/bison/BookshelfParser.cc"

#line 633 "BookshelfParser.yy"
 /*** Additional Code ***/

void BookshelfParser::Parser::error(const Parser::location_type& l,
			    const std::string& m)
{
    driver.error(l, m);
}
