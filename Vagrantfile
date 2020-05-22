# -*- mode: ruby -*-
# vi: set ft=ruby :

$INSTALL_CPP = <<SCRIPT
  apt-get install -y g++ cmake gdb gdbserver
SCRIPT

$INSTALL_MYSQL = <<SCRIPT
  sudo apt-get install mysql-server
SCRIPT

$INSTALL_BASE = <<SCRIPT
  sudo apt-get update
  sudo apt-get install -y build-essential vim emacs

  sudo add-apt-repository ppa:jonathonf/python-2.7
  sudo apt-get update
  sudo apt-get install python2.7
  sudo apt-get install -y python-pip
  sudo apt-get install -y git gdb valgrind python-dev libffi-dev libssl-dev
  sudo DEBIAN_FRONTEND=noninteractive apt-get -y install tshark
  sudo pip install --upgrade pip
  sudo pip install --default-timeout=1000000 -i https://pypi.tuna.tsinghua.edu.cn/simple tcconfig
  sudo pip install --default-timeout=1000000 -i https://pypi.tuna.tsinghua.edu.cn/simple scapy
  sudo pip install --default-timeout=1000000 -i https://pypi.tuna.tsinghua.edu.cn/simple pytest
  sudo pip install --default-timeout=1000000 -i https://pypi.tuna.tsinghua.edu.cn/simple fabric
  sudo pip install --default-timeout=1000000 -i https://pypi.tuna.tsinghua.edu.cn/simple cryptography==2.4.2

  sudo apt-get install -y python3-pip
  sudo pip3 install --upgrade pip
  sudo pip3 install --default-timeout=1000000 -i https://pypi.tuna.tsinghua.edu.cn/simple scapy
  sudo pip3 install --default-timeout=1000000 -i https://pypi.tuna.tsinghua.edu.cn/simple matplotlib 
  sudo apt-get install -y python-tk
SCRIPT

$INSTALL_IPERF = <<SCRIPT
  wget  https://github.com/esnet/iperf/archive/3.6.tar.gz
  tar -xvzf 3.6.tar.gz
  cd iperf-3*
  ./configure && make && sudo make install
  sudo apt-get -y remove lib32z1
  sudo apt-get -y install lib32z1
  cd ..
  sudo rm -r iperf-3* 3.6.tar.gz
SCRIPT

$INSTALL_OPENSSL = <<SCRIPT
  wget https://www.openssl.org/source/old/1.1.0/openssl-1.1.0g.tar.gz
  tar -xzvf openssl-1.1.0g.tar.gz
  cd openssl-1.1.0g
  ./config && make && sudo make install
  cd ..
  sudo rm -r *openssl-1.1.0g*
SCRIPT

Vagrant.configure(2) do |config|


  config.vm.box = "ubuntu/xenial64"
  config.ssh.forward_agent = true
  config.vm.provision "shell", inline: $INSTALL_BASE
  config.vm.provision "shell", inline: $INSTALL_CPP
  config.vm.provision "shell", inline: $INSTALL_IPERF
  config.vm.provision "shell", inline: $INSTALL_OPENSSL
  config.vm.synced_folder "15-441-project-2", "/vagrant/15-441-project-2"
  # config.vm.provider "virtualbox" do |vb|
  #   # Display the VirtualBox GUI when booting the machine
  #   vb.gui = true
  #
  #   # Customize the amount of memory on the VM:
  #   vb.memory = "1024"
  # end
  remote = "/vagrant"
  config.vm.synced_folder ".", remote, disabled: true
  config.vm.provision :shell, inline: "sudo sh -c '(mkdir #{remote} 2>/dev/null && chown vagrant:vagrant #{remote}) || true'"


  config.vm.define :client, primary: true do |host|
    port = 22000
    host.vm.hostname = "client"
    host.vm.network "private_network", ip: "10.0.0.2", netmask: "255.255.255.0", mac: "080027a7feb1",
                    virtualbox__intnet: "15441"

    host.vm.network "forwarded_port", id: "ssh", host: port, guest: 22, auto_correct: false

    host.vm.provision "shell", inline: "sudo tcset enp0s8 --rate 100Mbps --delay 20ms"
    host.vm.provision "shell", inline: "sudo sed -i 's/PasswordAuthentication no/PasswordAuthentication yes/g' /etc/ssh/sshd_config"
    host.vm.provision "shell", inline: "sudo service sshd restart"
  end

  config.vm.define :server do |host|
    port = 22001
    host.vm.hostname = "server"
    host.vm.network "private_network", ip: "10.0.0.1", netmask: "255.255.255.0", mac: "08002722471c",
                    virtualbox__intnet: "15441"

    host.vm.network "forwarded_port", id: "ssh", host: port, guest: 22, auto_correct: false

    host.vm.provision "shell", inline: "sudo tcset enp0s8 --rate 100Mbps --delay 20ms"
    host.vm.provision "shell", inline: "sudo sed -i 's/PasswordAuthentication no/PasswordAuthentication yes/g' /etc/ssh/sshd_config"
    host.vm.provision "shell", inline: "sudo service sshd restart"
  end
end
