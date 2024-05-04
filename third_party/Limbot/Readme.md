Limbo Library for VLSI CAD Design 
---------

All components are written with C/C++ and API is designed for easy usage and simple embedding. 
Please read this **Readme** file carefully for proper instructions to **install** and **customize**. 

- [Documentation](http://limbo018.github.io/Limbo). 
- [Github](https://github.com/limbo018/Limbo).

---------

# Packages 
| Packages                                | Languages                       | Description                                                                |
| --------------------------------------- | ------------------------------- | -------------------------------------------------------------------------- |
| Algorithms                              | C++                             | Useful algorithms including partitioning, coloring, etc.                   |
| Bibtex                                  | Python                          | Scripts convert bibtex into various formats                                |
| Containers                              | C++                             | Extension of contains from STL                                             |
| Geometry                                | C++                             | Geometric utilities and algorithms such as polygon-to-rectangle conversion |
| MakeUtils                               | Makefile                        | Makefile utilities that help find dependencies                             |
| Math                                    | C++                             | Extension of math functions from STL                                       |
| Parsers                                 | C++, Flex/Bison                 | Parsers to various formats, such as LEF/DEF, verilog, GDSII, etc.          |
| Preprocessor                            | C++                             | Macros such as assertion with message                                      |
| ProgramOptions                          | C++                             | Easy API to parser command line options                                    |
| Solvers                                 | C++                             | Solver wrap-ups such as SDP, LP solver with min-cost flow, etc.            |
| String                                  | C++                             | Utilities to char and string                                               |
| ThirdParty                              | C/C++, Fortran                  | Third party packages required                                              |

# Developers 

- [Yibo Lin](http://www.yibolin.com), School of EECS, Peking University

# Introduction 

## 1. Flex

* A fast scanner generator or lexical analyzer generator. Another famous related software is Lex.

## 2. Bison 

* A Yacc-compatible parser generator that is part of the GNU Project. 
	For details, please refer to [here](http://en.wikipedia.org/wiki/GNU_bison).
	Another famous related software is Yacc.

## 3. (*Deprecated*) Boost.Spirit

* An object-oriented, recursive-descent parser and output generation library for C++. 
	For details, please refer to [here](http://www.boost.org/doc/libs/1_55_0/libs/spirit/doc/html/index.html).

## 4. LL Parser 

* In computer science, a LL parser is a top-down parser for a subset of context-free languages. 
	It parses the input from Left to right, performing Leftmost derivation of the sentence. 

## 5. LR Parser 

* In computer science, LR parsers are a type of bottom-up parsers that efficiently handle 
	deterministic context-free languages in guaranteed linear time. 

## 6. LL v.s. LR

* Bison is based on LR parser and Boost.Spirit is based on LL parser.
Please refer to [here](http://cs.stackexchange.com/questions/43/language-theoretic-comparison-of-ll-and-lr-grammars).

## 7. LP and Min-Cost Flow 

* Min-cost flow (MCF) is a special case of linear programs (LP). 
It is usually faster than general LP solver and is also able to achieve integer solution.
Some LP can be directly transformed to MCF problem, and for some other LP, its duality is a MCF problem. 
In this library, an API for lemon (MFC solver) is implemented to solve LP and dual LP problems. 
The API supports reading files with both LGF (lemon graph format) and LP (linear program format) and then dump out solutions. 
Thus, it will be easier to verify results with general LP solvers such as Gurobi or CBC.

# Bug Report 

Please report bugs to [yibolin at utexas dot edu](mailto:yibolin@utexas.edu). 

# Installation 

Some components depend on external libraries, such as 

* [CMake](https://cmake.org): CMake version 3.7.0 or later is required to build the library. 
* [Boost](www.boost.org): managed by CMake [FindBoost](https://cmake.org/cmake/help/v3.13/module/FindBoost.html). 
* [Lemon](https://lemon.cs.elte.hu): integrated as a third party package. 
* [Gurobi](www.gurobi.com): require GUROBI_HOME environment variable to the path where Gurobi is installed. 
* [Flex](http://flex.sourceforge.net): managed by CMake [FindFlex](https://cmake.org/cmake/help/v3.13/module/FindFLEX.html). 
* [Zlib](http://www.zlib.net): managed by CMake [FindZLIB] (https://cmake.org/cmake/help/v3.13/module/FindZLIB.html). 

Users need to make sure they are properly installed and the corresponding settings are configured. 

## 1. Default installation

* In the directory of limbo library, run 
~~~~~~~~~~~~~~~~
mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=absolute/path/to/your/installation
make
make install 
~~~~~~~~~~~~~~~~

After installation, it is strongly recommended to export LIMBO_DIR to the path where Limbo library is installed as an environment variable. 

## 2. Customize OPENBLAS options 

* There is a third party OpenBLAS required by some other third party packages, such as Csdp. OPENBLAS option is used to control whether compiling these packages. 

If you do not need these packages, set -DOPENBLAS=0 (default); 
otherwise, set -DOPENBLAS=1.
The default version of OpenBLAS is not very stable for cross platforms, which often results in compiling errors. 
OpenBLAS is integrated as a submodule which fetch source code from remote repository. 
If you already have OpenBLAS in the directory, simply run "git submodule update" to fetch the latest version in the submodule directory. 

# FAQ 

##1. (*Deprecated*) Compiling errors like
~~~~~~~~~~~~~~~~
LefScanner.cc:5582:21: error: out-of-line definition of 'LexerInput' does not match any declaration in 'LefParserFlexLexer'
                        size_t yyFlexLexer::LexerInput( char* buf, size_t max_size )
~~~~~~~~~~~~~~~~

come from old versions of flex, such as 2.5.35. 

**A:** It can be solved by installing correct flex version 2.5.37 and add the directory to correct flex to PATH environment variable. 

##2. (*Deprecated*) Compiling errors like 
~~~~~~~~~~~~~~~~
LefScanner.cc:3195:8: error: member reference type 'std::istream *' (aka 'basic_istream<char> *') is a pointer; did you mean to use '->'?
                        yyin.rdbuf(std::cin.rdbuf());
~~~~~~~~~~~~~~~~

come from new versions of flex, such as 2.6.0. 

**A:** It can be solved by installing correct flex version 2.5.37 and add the directory to correct flex to PATH environment variable. 

##3. (*Deprecated*) Compiling errors related to LefScanner.cc usually come from the configurations of flex version and environment variables FLEX_DIR and LEX_INCLUDE_DIR. 

**A:** LefScanner.cc needs to include the correct FlexLexer.h from the flex package for compilation; i.e., the version of FlexLexer.h must match the version of the flex executable. 
Most errors for LefScanner.cc are caused by the failure of finding the correct FlexLexer.h (be careful when you have multiple versions of flex installed). 
To solve the problem, users can set the environment variable FLEX_DIR such that $FLEX_DIR/include points to the directory containing FlexLexer.h, or alternatively set LEX_INCLUDE_DIR to the directory containing FlexLexer.h. 
The decision can be made according to how the flex package is installed.  


##4. Crappy linkage error under gcc 5.1 or later, even though libxxx.a is correctly linked, like 
~~~~~~~~~~~~~~~~
undefined reference to `GdsParser::read(GdsParser::GdsDataBaseKernel&, std::string const&)'
~~~~~~~~~~~~~~~~

**A:** The issue probably comes from potential inconsistent compiling configuration for Limbo and target program due to the new [Dual ABI](https://gcc.gnu.org/onlinedocs/libstdc%2B%2B/manual/using_dual_abi.html) introduced since gcc 5.1. 
The compilation flag _GLIBCXX_USE_CXX11_ABI is used to control whether gcc uses new C++11 ABI, which is turned on in default. 
If Limbo is compiled with C++11 ABI, while target program is compiled with old ABI, then the linkage error appears, vice versa. 
Therefore, it is necessary to make sure the same STL ABI is used for compiling Limbo and target program. 
In other words, set consistent _GLIBCXX_USE_CXX11_ABI values. 
A safe way is to leave it to the default value. 

# Copyright 
The software is released under MIT license except third party packages. Please see the LICENSE file for details. 

Third party package **c-thread-pool** is released under MIT license. 

Third party package **csdp** is released under CPL v1.0 license. 

Third party package **OpenBLAS** has its copyright reserved; please check its license. 

# Tutorial 

Some components in limbo library do not need linkage, so they can be used directly by including the headers, while some components require linkage to the corresponding static libraries.  
Here are some simple example to show the basic usage and compiling commands with gcc under Linux. 
For clang, the compiling commands are slightly different as users need to specify the same **-stdlib** as that in **CXXSTDLIB** flag used to install the library. 
See pages of each package for examples. 

# LICENSE 

See LICENSE
