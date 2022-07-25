FROM registry.fedoraproject.org/fedora:latest

RUN set -e; \
  dnf install -y ansible python3-pip rpm-build mock csmock git; \
  pip3 install copr-builder; \
  git clone --depth 1 https://github.com/storaged-project/ci.git;

WORKDIR /
