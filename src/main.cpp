#include <iostream>
#include <boost/asio.hpp>

using namespace std;

int main(int argc, char* argv[]) {
    cout << "=== Nexus Ledger Node ===" << endl;
    cout << "Starting node ..." << endl;

    try
    {
        boost::asio::io_context io_context;

        io_context.run();

        cout << "Node stopped.";
    }
    catch(const exception& e)
    {
        cerr << "Error: " << e.what() << '\n';
    }
    
    return 0;
}