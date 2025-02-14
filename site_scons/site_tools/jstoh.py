import os
import sys


def jsToHeader(target, source):

    outFile = target

    h = [
        '#include "monger/base/string_data.h"',
        '#include "monger/scripting/engine.h"',
        'namespace monger {',
        'namespace JSFiles{',
    ]

    def lineToChars(s):
        return ','.join(str(ord(c)) for c in (s.rstrip() + '\n')) + ','

    for s in source:
        filename = str(s)
        objname = os.path.split(filename)[1].split('.')[0]
        stringname = '_jscode_raw_' + objname

        h.append('constexpr char ' + stringname + "[] = {")

        with open(filename, 'r') as f:
            for line in f:
                h.append(lineToChars(line))

        h.append("0};")
        # symbols aren't exported w/o this
        h.append('extern const JSFile %s;' % objname)
        h.append('const JSFile %s = { "%s", StringData(%s, sizeof(%s) - 1) };' %
                 (objname, filename.replace('\\', '/'), stringname, stringname))

    h.append("} // namespace JSFiles")
    h.append("} // namespace monger")
    h.append("")

    text = '\n'.join(h)

    with open(outFile, 'w') as out:
        try:
            out.write(text)
        finally:
            out.close()


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Must specify [target] [source] ")
        sys.exit(1)

    jsToHeader(sys.argv[1], sys.argv[2:])
