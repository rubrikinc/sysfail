FROM ubuntu:rolling
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update
RUN apt-get install -y build-essential g++ cmake libtbb-dev libgtest-dev
COPY . /sysfail
WORKDIR /sysfail
RUN rm -rf build
RUN mkdir build
WORKDIR /sysfail/build
RUN cmake ..
RUN make
RUN make test
RUN make install
