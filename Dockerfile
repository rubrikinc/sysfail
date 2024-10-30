FROM ubuntu:rolling
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update
RUN apt-get install -y build-essential g++ cmake libtbb-dev libgtest-dev
COPY . /sysfail
WORKDIR /sysfail
RUN rm -rf build
RUN mkdir build
WORKDIR /sysfail/build
RUN cmake .. -DCMAKE_INSTALL_PREFIX=/usr
RUN make -j4
RUN make test
RUN make install
RUN cp ../examples/flaky_print.cc /home/ubuntu/flaky_print.cc
RUN chown ubuntu:ubuntu /home/ubuntu/flaky_print.cc
