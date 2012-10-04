nexus
=====

Multi-Threaded Messaging Server based on Apache Qpid Proton(-c)


Introduction:

    Nexus is a native server based on Apache Qpid Proton-C.  It's purpose is
    to be a testbed for:

    1) Using Proton in a multi-threaded environment
    2) Building a general purpose AMQP container with customized nodes
    3) Message routing with AMQP
    4) Some approaches to multi-threading and memory/buffer management.

    The server now offers a set of three nailed-up FIFO queues which can be
    addressed for send or receive using the Proton messenger examples and an
    address like the following:

    amqp://<host>/queue1


Dependencies:

    Please note that this project depends on Qpid Proton-C.  At present,
    Proton has not yet been released.  In order to build nexus, you will need
    to manually set two variables in CMake to complete the linkage to Proton.

    Assuming you've built a checked-out version of Proton, the following CMake
    variables must be set:

    proton_include => <checked-out-proton>/proton-c/include
    proton_lib     => <proton-build-dir>/libqpid-proton.so

    Qpid proton may be found at the following URL:

    http://qpid.apache.org/proton


Known Issues:

    IMPORTANT - Nexus uses a facility that is not yet available in the
    upstream proton source code.  Nexus will not build until the following
    Jira is resolved:

    https://issues.apache.org/jira/browse/PROTON-39

    You can, of course, apply the patch from the Jira to your proton tree.

