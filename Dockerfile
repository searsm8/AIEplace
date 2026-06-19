FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Basic tools and dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    less \
    wget \
    gnupg \
    ca-certificates \
    lsb-release \
    git \
    build-essential \
    cmake \
    pkg-config \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

# Import Xilinx public key and add Xilinx repository
RUN wget -qO - https://www.xilinx.com/support/download/2020-2/xilinx-master-signing-key.asc \
      | apt-key add - && \
    echo "deb https://packages.xilinx.com/artifactory/debian-packages $(lsb_release -cs) main" \
      > /etc/apt/sources.list.d/xlnx.list && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
      xrt \
    && rm -rf /var/lib/apt/lists/*

# Optional: set default compiler symlinks
RUN update-alternatives --install /usr/bin/cc cc /usr/bin/gcc 100 && \
    update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++ 100

# Create non-root user; override UID/GID at build time
ARG USERNAME=dev
ARG UID=1000
ARG GID=1000

RUN groupadd -g ${GID} ${USERNAME} && \
    useradd -m -u ${UID} -g ${GID} -s /bin/bash ${USERNAME}

# Automatically source XRT setup for interactive shells
RUN echo 'if [ -f /opt/xilinx/xrt/setup.sh ]; then' >> /home/${USERNAME}/.bashrc && \
    echo '  . /opt/xilinx/xrt/setup.sh' >> /home/${USERNAME}/.bashrc && \
    echo 'fi' >> /home/${USERNAME}/.bashrc && \
    chown ${USERNAME}:${USERNAME} /home/${USERNAME}/.bashrc

USER ${USERNAME}
WORKDIR /workspace

CMD ["/bin/bash"]
