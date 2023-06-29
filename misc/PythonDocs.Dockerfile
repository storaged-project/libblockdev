# image for building documentation for Python bindings, see README.DEVEL.md for more details

FROM debian:testing
LABEL maintainer Vojtech Trefny <vtrefny@redhat.com>

# add deb-src repo
RUN echo "deb-src http://deb.debian.org/debian testing main" >> /etc/apt/sources.list

RUN apt-get update

# pgi-docgen dependencies
RUN apt-get -y install python3 python3-pip python3-jinja2 python3-sphinx python3-bs4 python3-graphviz libgirepository-1.0-1 gir1.2-glib-2.0

RUN apt-get -y install git

# latest pgi from git
RUN pip3 install "git+https://github.com/pygobject/pgi.git" --break-system-packages

WORKDIR /root

# install latest libblockdev
RUN git clone https://github.com/storaged-project/libblockdev

WORKDIR /root/libblockdev

# install libblockdev build dependencies
RUN apt-get -y install ansible
RUN apt-get -y install libnvme-dev
RUN ansible-playbook -K -i "localhost," -c local misc/install-test-dependencies.yml

RUN ./autogen.sh && ./configure --prefix=/usr && make -j6 && DEB_PYTHON_INSTALL_LAYOUT="deb" make install

WORKDIR /root

# get latest pgi-docgen and generate documentation for libblockdev
RUN git clone https://github.com/pygobject/pgi-docgen

WORKDIR /root/pgi-docgen

RUN ./tools/build.sh BlockDev-3.0
