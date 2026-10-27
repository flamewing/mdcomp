#include <mdcomp/comper.hh>
#include <mdcomp/comperx.hh>
#include <mdcomp/enigma.hh>
#include <mdcomp/kosinski.hh>
#include <mdcomp/kosplus.hh>
#include <mdcomp/lzkn1.hh>
#include <mdcomp/nemesis.hh>
#include <mdcomp/rocket.hh>
#include <mdcomp/saxman.hh>
#include <mdcomp/snkrle.hh>

#include <iostream>
#include <sstream>
#include <string>

template <typename Codec>
bool check() {
    std::string const  raw(64, char(0xa5));
    std::istringstream source(raw);
    std::stringstream  compressed, output;
    bool               encoded;
    if constexpr (requires { Codec::encode(source, compressed); }) {
        encoded = Codec::encode(source, compressed);
    } else {
        encoded = Codec::encode(source, compressed, true);    // Saxman framed entry.
    }
    return encoded && Codec::decode(compressed, output) && output.str() == raw;
}

int main() {
    try {
        return check<comper>() && check<comperx>() && check<enigma>() && check<kosinski>()
                               && check<kosplus>() && check<lzkn1>() && check<nemesis>()
                               && check<rocket>() && check<saxman>() && check<snkrle>()
                       ? 0
                       : 1;
    } catch (std::exception const& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
