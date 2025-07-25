# This program takes a C source as the input and produces the list of
# all signals registered.
#
# Output is:
#   <signal name="Changed">
#       <arg name="new_value" type="b"/>
#   </signal>
from __future__ import absolute_import, division, print_function
import re
import sys

# List "excluded" contains signals that shouldn't be exported via
# DBus.  If you remove a signal from this list, please make sure
# that it does not break "make" with the configure option
# "--enable-dbus" turned on.

excluded = [\
    # purple_dbus_signal_emit_purple prevents our "dbus-method-called"
    # signal from being propagated to dbus.
	"dbus-method-called",
    ]

registerregex = re.compile(r"purple_signal_register[^;]+\"([\w\-]+)\"[^;]+(purple_marshal_\w+)[^;]+;")
nameregex = re.compile(r'[-_][a-z]')

print("/* Generated by %s.  Do not edit! */" % sys.argv[0])
print("const char *dbus_signals = ")
for match in registerregex.finditer(sys.stdin.read()):
    signal = match.group(1)
    marshal = match.group(2)
    if signal in excluded:
        continue

    signal = nameregex.sub(lambda x:x.group()[1].upper(), '-'+signal)
    print("\"    <signal name='%s'>\\n\"" % signal)

    args = marshal.split('_')
    # ['purple', 'marshal', <return type>, '', args...]
    if len(args) > 4:
        for arg in args[4:]:
            if arg == "POINTER":
                type = 'p'
            elif arg == "ENUM":
                type = 'i'
            elif arg == "INT":
                type = 'i'
            elif arg == "UINT":
                type = 'u'
            elif arg == "INT64":
                type = 'x'
            elif arg == "UINT64":
                type = 't'
            elif arg == "BOOLEAN":
                type = 'b'
            print("\"      <arg type='%s'/>\\n\"" % type)

    print("\"    </signal>\\n\"")

print(";")
