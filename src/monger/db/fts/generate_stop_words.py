import sys

def generate( header, source, language_files ):
    out = open( header, "w" )
    out.write( """
#pragma once
#include <set>
#include <string>
#include "monger/util/string_map.h"
namespace monger {
namespace fts {

  void loadStopWordMap( StringMap< std::set< std::string > >* m );
}
}
""" )
    out.close()



    out = open( source, "w", encoding='utf-8')
    out.write( '#include "{}"'.format(header.rpartition( "/" )[2].rpartition( "\\" )[2]) )
    out.write( """
namespace monger {
namespace fts {

  void loadStopWordMap( StringMap< std::set< std::string > >* m ) {

""" )

    for l_file in language_files:
        l = l_file.rpartition( "_" )[2].partition( "." )[0]

        out.write( '  // %s\n' % l_file )
        out.write( '  {\n' )
        out.write( '   const char* const words[] = {\n' )
        for word in open( l_file, "rb" ):
            out.write( '       "%s",\n' % word.decode('utf-8').strip() )
        out.write( '   };\n' )
        out.write( '   const size_t wordcnt = sizeof(words) / sizeof(words[0]);\n' )
        out.write( '   std::set< std::string >& l = (*m)["%s"];\n' % l )
        out.write( '   l.insert(&words[0], &words[wordcnt]);\n' )
        out.write( '  }\n' )

    out.write( """
  }
} // namespace fts
} // namespace monger
""" )


if __name__ == "__main__":
    generate( sys.argv[ len(sys.argv) - 2],
              sys.argv[ len(sys.argv) - 1],
              sys.argv[1:-2] )
