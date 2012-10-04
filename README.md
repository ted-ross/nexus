nexus
=====

Multi-Threaded Messaging Server based on Apache Qpid Proton(-c)


Dependencies:

    Please note that this project depends on Qpid Proton-C.  At
    present, Proton has not yet been released.  In order to build
    nexus, you will need to manually set two variables in CMake to
    complete the linkage to Proton.

    Assuming you've built a checked-out version of Proton, the
    following CMake variables must be set:

    proton_include  => <checked-out-proton>/proton-c/include
    proton_lib      => <proton-build-dir>/libqpid-proton.so

    Qpid proton may be found at the following URL:

    http://qpid.apache.org/proton


Known Issues:

    IMPORTANT - Nexus uses a facility that is not yet available in the
    upstream proton source code.  Nexus will not build until the
    following Jira is resolved:

    https://issues.apache.org/jira/browse/PROTON-39

    You can, of course, apply the patch from the Jira to your proton tree.

