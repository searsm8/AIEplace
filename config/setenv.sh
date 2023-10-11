#/bin/bash
root=$(readlink -f $(dirname ${BASH_SOURCE[0]}))
arch=$(uname -m)

set_path_only=0
host=`hostname`
OSDIST=`lsb_release -i |awk -F: '{print tolower($2)}' | tr -d ' \t'`
OSREL=`lsb_release -r |awk -F: '{print tolower($2)}' |tr -d ' \t'`
default_dir=${arch}/centos-default

if [[ $OSDIST == "ubuntu" ]]; then
    
    if [[ $OSREL != "16.04" ]] &&  [[ $OSREL != "18.04" ]] &&  [[ $OSREL != "20.04" ]]; then
        echo "Ubuntu $OSREL detected (${host}), minimal setup"
        set_path_only=1
    fi
fi

if [[ $OSDIST == "centos" ]] || [[ $OSDIST == "redhat"* ]]; then
    if [[ $OSREL != "7.4"* ]] &&  [[ $OSREL != "7.5"* ]] &&  [[ $OSREL != "7.6"* ]] &&  [[ $OSREL != "7.7"* ]] &&  [[ $OSREL != "7.8"* ]] &&  [[ $OSREL != "7.9"* ]] &&  [[ $OSREL != "8.1"* ]] &&  [[ $OSREL != "8.2"* ]] &&  [[ $OSREL != "8.3"* ]] &&  [[ $OSREL != "8.4"* ]] ; then
        echo "Centos/RHEL $OSREL detected (${host}), minimal setup"
        set_path_only=1
    fi
    # only need major.manor
    OSREL=`echo $OSREL | awk -F '.' '{print $1"."$2}'`
    # default CentOS is 7.8
    if [[ $OSREL == "7."* ]] ; then
      OSREL="7.8"
    else
      OSREL="8.1"
    fi
    #CentOS = RHEL
    OSDIST="centos"

fi

dir=${arch}/xrt-2.13.466_${OSDIST}_${OSREL}

if [ ! -d "${root}/${dir}" ]; then
  tmpDir=${arch}/${OSDIST}-default
  echo "## ${dir} does not exists trying default for: ${OSDIST}"
  if [ ! -d "${root}/${tmpDir}" ]; then
    echo "# Default directory (${tmpDir}) for ${OSDIST} does not exists will use common directory: ${default_dir} "
    dir=${default_dir}
  else
    echo "# Default directory (${default_dir}) for ${OSDIST} exists will use it."
    dir=${tmpDir}
  fi
fi

root=${root}/$dir
if [ $set_path_only == 0 ]; then
  TEMP=${XILINX_SDX}
  echo "Sourcing: ${root}/opt/xilinx/xrt/setup.sh"
  . ${root}/opt/xilinx/xrt/setup.sh
  export XILINX_SDX=${TEMP}
else
  export XILINX_XRT=${root}/opt/xilinx/xrt
  export PATH=$XILINX_XRT/bin:$PATH
  echo "XILINX_XRT      : $XILINX_XRT"
  echo "PATH            : $PATH"
fi

