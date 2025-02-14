artifacts_tarball = 'artifacts.tgz'
user = node['current_user']
homedir = node['etc']['passwd'][user]['dir']

ruby_block 'allow sudo over tty' do
  block do
    file = Chef::Util::FileEdit.new('/etc/sudoers')
    file.search_file_replace_line(/Defaults\s+requiretty/, '#Defaults requiretty')
    file.search_file_replace_line(/Defaults\s+requiretty/, '#Defaults !visiblepw')
    file.write_file
  end
end

# This file limits processes to 1024. It therefore interfereres with `ulimit -u` when present.
if platform_family? 'rhel'
  file '/etc/security/limits.d/90-nproc.conf' do
    action :delete
  end
end

remote_file "#{homedir}/#{artifacts_tarball}" do
  source node['artifacts_url']
end

execute 'extract artifacts' do
  command "tar xzvf #{artifacts_tarball}"
  cwd homedir
end

if platform_family? 'debian'

  # SERVER-40491 Debian 8 sources.list need to point to archive url
  if node['platform_version'] == '8.1'
    cookbook_file '/etc/apt/sources.list' do
      source 'sources.list.debian8'
      owner 'root'
      group 'root'
      mode '0644'
      action :create
    end
  end

  execute 'apt-get update' do
    command 'apt-get update'
  end

  package 'openssl'

  # dpkg returns 1 if dependencies are not satisfied, which they will not be
  # for enterprise builds. We install dependencies in the next block.
  execute 'install mongerd' do
    command 'dpkg -i `find . -name "*server*.deb"`'
    cwd homedir
    returns [0, 1]
  end

  # install the tools so we can test install_compass
  execute 'install monger tools' do
    command 'dpkg -i `find . -name "*tools*.deb"`'
    cwd homedir
    returns [0, 1]
  end

  # yum and zypper fetch dependencies automatically, but dpkg does not.
  # Installing the dependencies explicitly is fragile, so we reply on apt-get
  # to install dependencies after the fact.
  execute 'install dependencies' do
    command 'apt-get update && apt-get -y -f install'
  end

  # the ubuntu 16.04 image does not have python installed by default
  # and it is required for the install_compass script
  execute 'install python' do
    command 'apt-get install -y python'
  end

  execute 'install monger shell' do
    command 'dpkg -i `find . -name "*shell*.deb"`'
    cwd homedir
  end
end

if platform_family? 'rhel'
  execute 'install mongerd' do
    command 'yum install -y `find . -name "*server*.rpm"`'
    cwd homedir
  end

  # install the tools so we can test install_compass
  execute 'install monger tools' do
    command 'yum install -y `find . -name "*tools*.rpm"`'
    cwd homedir
  end

  execute 'install monger shell' do
    command 'yum install -y `find . -name "*shell*.rpm"`'
    cwd homedir
  end
end

if platform_family? 'suse'
  bash 'wait for zypper lock to be released' do
    code <<-EOD
    retry_counter=0
    # We also need to make sure another instance of zypper isn't running while
    # we do our install, so just run zypper refresh until it doesn't fail.
    # Waiting for 2 minutes is copied from an internal project where we do this.
    until [ "$retry_counter" -ge "12" ]; do
        zypper refresh && exit 0
        retry_counter=$(($retry_counter + 1))
        [ "$retry_counter" = "12" ] && break
        sleep 10
    done
    exit 1
  EOD
  end

  %w(
     SLES12-Pool
     SLES12-Updates
  ).each do |repo|
    execute "add #{repo}" do
      command "zypper addrepo --check --refresh --name \"#{repo}\" http://smt-ec2.susecloud.net/repo/SUSE/Products/SLE-SERVER/12/x86_64/product?credentials=SMT-http_smt-ec2_susecloud_net 'SMT-http_smt-ec2_susecloud_net:#{repo}'"
      not_if "zypper lr | grep #{repo}"
    end
  end

  execute 'install mongerd' do
    command 'zypper -n install `find . -name "*server*.rpm"`'
    cwd homedir
  end

  execute 'install monger' do
    command 'zypper -n install `find . -name "*shell*.rpm"`'
    cwd homedir
  end
end

inspec_wait = <<HEREDOC
#!/bin/bash
ulimit -v unlimited
for i in {1..60}
do
  monger --eval "db.smoke.insert({answer: 42})"
  if [ $? -eq 0 ]
  then
    exit 0
  else
    echo "sleeping"
    sleep 1
  fi
done
exit 1
HEREDOC

file '/inspec_wait.sh' do
  content inspec_wait
  mode '0755'
end
