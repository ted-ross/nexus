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

    The target executable, nexus-router, is a message router that registers
    addresses based on outbound links (subscriptions) and forwards all messages
    to their destinations.  Addresses currently have the form:

    amqp://<host>/any-text


Dependencies:

    Please note that this project depends on Qpid Proton-C.  Nexus can be built
    against both 0.1 and 0.2 versions of Proton (and will track versions going forward).

    If you wish to use a source-built version of Proton, you will need
    to manually set two variables in CMake to complete the linkage to Proton.

    Assuming you've built a checked-out version of Proton, the following CMake
    variables must be set:

    proton_include => <checked-out-proton>/proton-c/include
    proton_lib     => <proton-build-dir>/libqpid-proton.so

    Qpid proton may be found at the following URL:

    http://qpid.apache.org/proton

