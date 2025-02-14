############################################################
# This section verifies start, stop, and restart.
# - stop mongerd so that we begin testing from a stopped state
# - verify start, stop, and restart
############################################################

# service is not in path for commands with sudo on suse
service = os[:name] == 'suse' ? '/sbin/service' : 'service'

describe command("#{service} mongerd stop") do
  its('exit_status') { should eq 0 }
end

describe command("#{service} mongerd start") do
  its('exit_status') { should eq 0 }
end

# Inspec treats all amazon linux as upstart, we explicitly make it use
# systemd_service https://github.com/chef/inspec/issues/2639
if (os[:name] == 'amazon' and os[:release] == '2.0')
  describe systemd_service('mongerd') do
    it { should be_running }
  end
else
  describe service('mongerd') do
    it { should be_running }
  end
end

describe command("#{service} mongerd stop") do
  its('exit_status') { should eq 0 }
end

if (os[:name] == 'amazon' and os[:release] == '2.0')
  describe systemd_service('mongerd') do
    it { should_not be_running }
  end
else
  describe service('mongerd') do
    it { should_not be_running }
  end
end

describe command("#{service} mongerd restart") do
  its('exit_status') { should eq 0 }
end

if (os[:name] == 'amazon' and os[:release] == '2.0')
  describe systemd_service('mongerd') do
    it { should be_running }
  end
else
  describe service('mongerd') do
    it { should be_running }
  end
end

if os[:arch] == 'x86_64' and os[:name] != 'amazon' and
  ((os[:name] == 'ubuntu' and os[:release].split('.')[0].to_i > 12) or 
    (os[:family] == 'redhat' and os[:release].split('.')[0].to_i >= 7))
  describe command("install_compass") do
    its('exit_status') { should eq 0 }
  end
else
  describe command("install_compass") do
    its('exit_status') { should_not eq 0 }
  end
end

# wait to make sure mongerd is ready
describe command("/inspec_wait.sh") do
  its('exit_status') { should eq 0 }
end

############################################################
# This section verifies files, directories, and users
# - files and directories exist and have correct attributes
# - mongerd user exists and has correct attributes
############################################################

# convenience variables for init system and package type
upstart = (os[:name] == 'ubuntu' && os[:release][0..1] == '14') ||
          (os[:name] == 'amazon')
sysvinit = if (os[:name] == 'debian' && os[:release][0] == '7') ||
              (os[:name] == 'redhat' && os[:release][0] == '6') ||
              (os[:name] == 'suse' && os[:release][0..1] == '11') ||
              (os[:name] == 'ubuntu' && os[:release][0..1] == '12')
             true
           else
             false
           end
systemd = !(upstart || sysvinit)
rpm = if os[:name] == 'amazon' || os[:name] == 'redhat' || os[:name] == 'suse'
        true
      else
        false
      end
deb = !rpm

# these files should exist on all systems
%w(
  /etc/mongerd.conf
  /usr/bin/mongerd
  /var/log/mongerdb/mongerd.log
).each do |filename|
  describe file(filename) do
    it { should be_file }
  end
end

if sysvinit
  describe file('/etc/init.d/mongerd') do
    it { should be_file }
    it { should be_executable }
  end
end

if systemd
  describe file('/lib/systemd/system/mongerd.service') do
    it { should be_file }
  end
end

if rpm
  %w(
    /var/lib/monger
    /var/run/mongerdb
  ).each do |filename|
    describe file(filename) do
      it { should be_directory }
    end
  end

  describe user('mongerd') do
    it { should exist }
    its('groups') { should include 'mongerd' }
    its('home') { should eq '/var/lib/monger' }
    its('shell') { should eq '/bin/false' }
  end
end

if deb
  describe file('/var/lib/mongerdb') do
    it { should be_directory }
  end

  if os[:release] == '18.04'
    describe user('mongerdb') do
      it { should exist }
      its('groups') { should include 'mongerdb' }
      its('shell') { should eq '/usr/sbin/nologin' }
    end
  else
    describe user('mongerdb') do
      it { should exist }
      its('groups') { should include 'mongerdb' }
      its('shell') { should eq '/bin/false' }
    end
  end
end

############################################################
# This section verifies ulimits.
############################################################

ulimits = {
  'Max file size'     => 'unlimited',
  'Max cpu time'      => 'unlimited',
  'Max address space' => 'unlimited',
  'Max open files'    => '64000',
  'Max resident set'  => 'unlimited',
  'Max processes'     => '64000'
}
ulimits_cmd = 'cat /proc/$(pgrep mongerd)/limits'

ulimits.each do |limit, value|
  describe command("#{ulimits_cmd} | grep \"#{limit}\"") do
    its('stdout') { should match(/#{limit}\s+#{value}/) }
  end
end

############################################################
# This section verifies reads and writes.
# - insert a document into the database
# - verify that findOne() returns a matching document
############################################################

describe command('sh -c "ulimit -v unlimited && monger --eval \"db.smoke.insert({answer: 42})\""') do
  its('exit_status') { should eq 0 }
  its('stdout') { should match(/.+WriteResult\({ "nInserted" : 1 }\).+/m) }
end

# read a document from the db
describe command('sh -c "ulimit -v unlimited && monger --eval \"db.smoke.findOne()\""') do
  its('exit_status') { should eq 0 }
  its('stdout') { should match(/.+"answer" : 42.+/m) }
end

############################################################
# This section verifies uninstall.
############################################################

if rpm
  describe command('rpm -e $(rpm -qa | grep "mongerdb.*server" | awk \'{print $1}\')') do
    its('exit_status') { should eq 0 }
  end
elsif deb
  describe command('dpkg -r $(dpkg -l | grep "mongerdb.*server" | awk \'{print $2}\')') do
    its('exit_status') { should eq 0 }
  end
end

# make sure we cleaned up
%w(
  /lib/systemd/system/mongerd.service
  /usr/bin/mongerd
).each do |filename|
  describe file(filename) do
    it { should_not exist }
  end
end
