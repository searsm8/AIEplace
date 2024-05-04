/**
 * @file   def/test_adapt.cpp
 * @brief  test def parser, see @ref DefParser::Driver and @ref DefParser::DefDataBase
 * @author Yibo Lin
 * @date   Oct 2014
 */

#include <iostream>
#include <fstream>

#include <limbo/parsers/def/adapt/DefDriver.h>

using std::cout;
using std::cin;
using std::endl;
using std::string;

/// @brief Custom class that inheritates @ref DefParser::DefDataBase 
/// with all the required callbacks defined. 
class DefDataBase : public DefParser::DefDataBase
{
	public:
        /// @brief constructor 
		DefDataBase()
		{
			cout << "DefDataBase::" << __func__ << endl;
		}
		//////////////////// required callbacks from abstract DefParser::DefDataBase ///////////////////
        /// @param token divider characters 
		virtual void set_def_dividerchar(string const& token) 
		{
			cout << __func__ << " => " << token << endl;
		}
        /// @param token BUS bit characters 
		virtual void set_def_busbitchars(string const& token)
		{
			cout << __func__ << " => " << token << endl;
		}
        /// @param token DEF version 
		virtual void set_def_version(string const& token) 
		{
			cout << __func__ << " => " << token << endl;
		}
        /// @param token design name 
		virtual void set_def_design(string const& token) 
		{
			cout << __func__ << " => " << token << endl;
		}
        /// @param token DEF unit 
		virtual void set_def_unit(int token) 
		{
			cout << __func__ << " => " << token << endl;
		}
        /// @param t1, t2, t3, t4 die area (xl, yl, xh, yh)
		virtual void set_def_diearea(int t1, int t2, int t3, int t4) 
		{
			cout << __func__ << " => " << t1 << "," << t2 << "," << t3 << "," << t4 << endl;
		}
        /// @brief add row 
		virtual void add_def_row(DefParser::Row const&) 
		{
			cout << __func__ << endl;
		}
        /// @brief add component 
        /// @param c component 
		virtual void add_def_component(DefParser::Component const& c) 
		{
			cout << __func__ << ": " << c.comp_name << ": status = " << c.status << endl;
		}
        /// @param token number of components 
		virtual void resize_def_component(int token) 
		{
			cout << __func__ << " => " << token << endl;
		}
        /// @brief add pin 
        /// @param p pin 
		virtual void add_def_pin(DefParser::Pin const& p) 
		{
			cout << __func__ << ": " << p.pin_name << endl;
		}
        /// @brief set number of pins 
        /// @param token number of pins 
		virtual void resize_def_pin(int token)  
		{
			cout << __func__ << " => " << token << endl;
		}
        /// @brief add net 
        /// @param n net 
		virtual void add_def_net(DefParser::Net const& n) 
		{
			cout << __func__ << ": " << n.net_name << ": weight " << n.net_weight << endl;
		}
        /// @brief set number of nets 
        /// @param token number of nets 
		virtual void resize_def_net(int token) 
		{
			cout << __func__ << " => " << token << endl;
		}
        /// @brief set number of blockages 
        /// @param n number of blockages 
        virtual void resize_def_blockage(int n)
        {
			cout << __func__ << " => " << n << endl;
        }
        /// @brief add placement blockages 
        /// @param vBbox array of boxes with xl, yl, xh, yh
        virtual void add_def_placement_blockage(std::vector<std::vector<int> > const& vBbox)
        {
            cout << __func__ << " => ";
            for (std::vector<std::vector<int> >::const_iterator it = vBbox.begin(); it != vBbox.end(); ++it)
                cout << "(" << (*it)[0] << ", " << (*it)[1] << ", " << (*it)[2] << ", " << (*it)[3] << ") ";
            cout << endl; 
        }
        /// @brief set number of regions 
        virtual void resize_def_region(int n)
        {
			cout << __func__ << " => " << n << endl;
        }
        /// @brief add region 
        virtual void add_def_region(DefParser::Region const& region)
        {
            cout << __func__ << "\n" << region; 
        }
        /// @brief set number of groups 
        virtual void resize_def_group(int n)
        {
			cout << __func__ << " => " << n << endl;
        }
        /// @brief add group 
        virtual void add_def_group(DefParser::Group const& group)
        {
            cout << __func__ << "\n" << group; 
        }
        /// @brief end of design 
        virtual void end_def_design()
        {
            cout << __func__ << endl; 
        }
};

/// @brief test 1: use function wrapper @ref DefParser::read  
void test1(string const& filename)
{
	cout << "////////////// test1 ////////////////" << endl;
	DefDataBase db;
	DefParser::read(db, filename);
}

/// @brief test 2: use class wrapper @ref DefParser::Driver 
void test2(string const& filename)
{
	cout << "////////////// test2 ////////////////" << endl;
	DefDataBase db;
	DefParser::Driver driver (db);
	//driver.trace_scanning = true;
	//driver.trace_parsing = true;

	driver.parse_file(filename);
}

/// @brief main function 
/// @param argc number of arguments 
/// @param argv values of arguments 
/// @return 0 
int main(int argc, char** argv)
{

	if (argc > 1)
	{
		test1(argv[1]);
		test2(argv[1]);
	}
	else 
		cout << "at least 1 argument is required" << endl;

	return 0;
}
