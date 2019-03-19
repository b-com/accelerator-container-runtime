Name: accelerator-container-runtime
Version: %{version}
Release: %{release}
Group: Development Tools

Vendor: b<>com
#Packager: b<>com <acceleration@b-com.com>

Summary: FPGA accelerators container runtime
URL: https://github.com/b-com/accelerator-container-runtime
License: BSD

Source0: accelerator-container-runtime
Source1: accelerator-container-runtime-hook
Source2: accelerator-container-runtime-tool
Source3: acceleration.json
Source4: daemon.json
#Source5: LICENSE

#Obsoletes: accelerator-container-runtime < 2.0.0
Requires: json-c

%description
Provides a OCI hook to enable FPGA accelerator support in containers.

%prep
cp %{SOURCE0} %{SOURCE1} %{SOURCE2} %{SOURCE3} %{SOURCE4} .

%install
mkdir -p %{buildroot}%{_bindir}
install -m 755 -t %{buildroot}%{_bindir} accelerator-container-runtime
install -m 755 -t %{buildroot}%{_bindir} accelerator-container-runtime-hook
install -m 755 -t %{buildroot}%{_bindir} accelerator-container-runtime-tool
mkdir -p %{buildroot}/etc/accelerator-container-runtime
install -m 644 -t %{buildroot}/etc acceleration.json
mkdir -p  %{buildroot}/etc/docker
install -m 644 -t %{buildroot}/etc/docker daemon.json

%files
#%license LICENSE
%{_bindir}/accelerator-container-runtime
%{_bindir}/accelerator-container-runtime-hook
%{_bindir}/accelerator-container-runtime-tool
/etc/acceleration.json
/etc/docker/daemon.json

%changelog
