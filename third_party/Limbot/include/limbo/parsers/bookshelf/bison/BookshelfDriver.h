/**
 * @file   BookshelfDriver.h
 * @brief  Driver for Bookshelf parser 
 * @author Yibo Lin
 * @date   Oct 2014
 */

#ifndef BOOKSHELFPARSER_DRIVER_H
#define BOOKSHELFPARSER_DRIVER_H

#include "BookshelfDataBase.h"

/** The example namespace is used to encapsulate the three parser classes
 * example::Parser, example::Scanner and example::Driver */
/** @brief namespace for BookshelfParser */
namespace BookshelfParser {

using std::cout;
using std::endl;
using std::cerr;
using std::string; 
using std::vector;
using std::pair;
using std::make_pair;
using std::ostringstream;

/**
 * @class BookshelfParser::Driver
 * The Driver class brings together all components. It creates an instance of
 * the Parser and Scanner classes and connects them. Then the input stream is
 * fed into the scanner object and the parser gets it's token
 * sequence. Furthermore the driver object is available in the grammar rules as
 * a parameter. Therefore the driver class contains a reference to the
 * structure into which the parsed data is saved. 
 */
class Driver
{
public:
    /// construct a new parser driver context
    /// @param db reference to database 
    Driver(BookshelfDataBase& db);

    /// enable debug output in the flex scanner
    bool trace_scanning;

    /// enable debug output in the bison parser
    bool trace_parsing;

    /// stream name (file or input stream) used for error messages.
    string streamname;

    /** Invoke the scanner and parser for a stream.
     * @param in	input stream
     * @param sname	stream name for error messages
     * @return		true if successfully parsed
     */
    bool parse_stream(std::istream& in,
		      const string& sname = "stream input");

    /** Invoke the scanner and parser on an input string.
     * @param input	input string
     * @param sname	stream name for error messages
     * @return		true if successfully parsed
     */
    bool parse_string(const string& input,
		      const string& sname = "string stream");

    /** Invoke the scanner and parser on a file. Use parse_stream with a
     * std::ifstream if detection of file reading errors is required.
     * @param filename	input file name
     * @return		true if successfully parsed
     */
    bool parse_file(const string& filename);

    // To demonstrate pure handling of parse errors, instead of
    // simply dumping them on the standard error output, we will pass
    // them to the driver using the following two member functions.

    /** Error handling with associated line number. This can be modified to
     * output the error e.g. to a dialog box. */
    void error(const class location& l, const string& m);

    /** General error handling. This can be modified to output the error
     * e.g. to a dialog box. */
    void error(const string& m);

    /** Pointer to the current lexer instance, this is used to connect the
     * parser to the scanner. It is used in the yylex macro. */
    class Scanner* lexer;

    /** Reference to the database filled during parsing of the
     * expressions. */
    BookshelfDataBase& m_db;

    /// @brief control m_plFlag
    /// @param flag control flag 
    void setPlFlag(bool flag);
    // .nodes file 
    /// @brief from .nodes file, number of node and terminals 
    void numNodeTerminalsCbk(int, int);
    /// @brief from .nodes file, terminal entry 
    void terminalEntryCbk(string&, int, int);
    /// @brief from .nodes file, terminal_NI entry 
    void terminalNIEntryCbk(string&, int, int);
    /// @brief from .nodes file, node entry 
    void nodeEntryCbk(string&, int, int, string&);
    /// @brief from .nodes file, node entry 
    void nodeEntryCbk(string&, int, int);
    // .nets file 
    /// @brief from .nets file, number of nets 
    void numNetCbk(int);
    /// @brief from .nets file, number of pins  
    void numPinCbk(int);
    /// @brief from .nets file, entry of net and pin 
    void netPinEntryCbk(string&, char, double, double, double, double, string&);
    /// @brief from .nets file, entry of net and pin 
    void netPinEntryCbk(string&, char, double, double, double=0.0, double=0.0);
    /// @brief from .nets file, net name and degree 
    void netNameAndDegreeCbk(string&, int);
    /// @brief from .nets file, net entry 
    void netEntryCbk();
    // .pl file 
    /// @brief from .pl file, node entry in placement 
    void plNodeEntryCbk(string&, double, double, string&, string&);
    /// @brief from .pl file, node entry in placement 
    void plNodeEntryCbk(string&, double, double, string&);
    // .scl file 
    /// @brief from .scl file, number of rows 
    void sclNumRows(int);
    /// @brief from .scl file, core row start 
    void sclCoreRowStart(string const&);
    /// @brief from .scl file, core row coordinate  
    void sclCoreRowCoordinate(int);
    /// @brief from .scl file, core row height 
    void sclCoreRowHeight(int);
    /// @brief from .scl file, site width  
    void sclCoreRowSitewidth(int);
    /// @brief from .scl file, site spacing 
    void sclCoreRowSitespacing(int);
    /// @brief from .scl file, site orientation 
    void sclCoreRowSiteorient(int);
    /// @brief from .scl file, site orientation 
    void sclCoreRowSiteorient(string&);
    /// @brief from .scl file, site symmetry
    void sclCoreRowSitesymmetry(int);
    /// @brief from .scl file, site symmetry
    void sclCoreRowSitesymmetry(string&);
    /// @brief from .scl file, subrow origin 
    void sclCoreRowSubRowOrigin(int);
    /// @brief from .scl file, number of sites 
    void sclCoreRowNumSites(int);
    /// @brief from .scl file, end of core row 
    void sclCoreRowEnd();
    // .wts file 
    /// @brief from .wts file, net weight entry 
    void wtsNetWeightEntry(string&, double);
    /// .shapes file 
    /// @brief from .shapes file, number of nodes with non-rectangular shapes 
    void shapesNumNonRectangularNodesCbk(int);
    /// @brief from .shapes file, one box entry 
    void shapesEntryCbk(string&, double, double, double, double);
    /// @brief from .shapes file, node name and number of boxes
    void shapesNodeNameCbk(string&, int);
    /// @brief from .shapes file, end file 
    void shapesEndCbk();
    /// @brief from .route file, Global routing grid (num_X_grids num_Y_grids num_layers)
    void routeGridCbk(int, int, int); 
    /// @brief from .route file, Vertical capacity per tile edge on each layer 
    void routeVerticalCapacityCbk(IntegerArray&);
    /// @brief from .route file, Horizontal capacity per tile edge on each layer 
    void routeHorizontalCapacityCbk(IntegerArray&); 
    /// @brief from .route file, Minimum metal width on each layer 
    void routeMinWireWidthCbk(IntegerArray&);
    /// @brief from .route file, Minimum spacing on each layer 
    void routeMinWireSpacingCbk(IntegerArray&);
    /// @brief from .route file, Via spacing per layer 
    void routeViaSpacingCbk(IntegerArray&); 
    /// @brief from .route file, Absolute coordinates of the origin of the grid (grid_lowerleft_X grid_lowerleft_Y)
    void routeGridOriginCbk(double, double);
    /// @brief from .route file, tile_width tile_height 
    void routeTileSizeCbk(double, double); 
    /// @brief from .route file, Porosity for routing blockages
    /// (Zero implies the blockage completely blocks overlapping routing tracks. Default = 0)
    void routeBlockagePorosityCbk(int); 
    /// @brief from .route file, number of IO pins  
    void routeNumNiTerminalsCbk(int); 
    /// @brief from .route file, for IO pins, (node_name layer_id_for_all_node_pins) 
    void routePinLayerCbk(string&, int); 
    void routePinLayerCbk(string&, string&); 
    /// @brief from .route file, number of blockage nodes
    void routeNumBlockageNodes(int); 
    /// @brief from .route file, for blockages, (node_name num_blocked_layers list_of_blocked_layers) 
    void routeBlockageNodeLayerCbk(string&, int, IntegerArray&); 
    void routeBlockageNodeLayerCbk(string&, int, StringArray&); 
    // .aux file 
    /// @brief from .aux file, other bookshelf files 
    void auxCbk(string&, vector<string>&);

    /// get all bookshelf files except .aux 
    /// @return bookshelf files except .aux 
    vector<string> const& bookshelfFiles() const {return m_vBookshelfFiles;}
protected:
	Row m_row; ///< temporary storage of row 
	Net m_net; ///< temporary storage of net 
    NodeShape m_shape; ///< temporary storage of shape 
    RouteInfo m_routeInfo; ///< temporary storage of routing information 
    vector<string> m_vBookshelfFiles; ///< store bookshelf files except .aux 
    bool m_plFlag; ///< if true, indicate that only reads .pl file, this will result in different callbacks in the database 
};

/// @brief API for BookshelfParser. 
/// Read .aux file and parse all other files. 
/// @param db database which is derived from @ref BookshelfParser::BookshelfDataBase
/// @param auxFile .aux file 
bool read(BookshelfDataBase& db, const string& auxFile);
/// @brief Read .pl file only, the callback only provide positions and orientation. 
/// @param db database which is derived from @ref BookshelfParser::BookshelfDataBase
/// @param plFile .pl file 
bool readPl(BookshelfDataBase& db, const string& plFile);

} // namespace example

#endif // EXAMPLE_DRIVER_H
