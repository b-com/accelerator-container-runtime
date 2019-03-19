FROM centos:7

RUN yum -y install json-c 

# Intel test programs 
# (manual install as opae*-tools.rpm has unjustified opae*-devel dependency)
COPY opae*-tools.rpm /tmp/
RUN cd / && rpm2cpio /tmp/opae*-tools.rpm | cpio -id && cd -
COPY hello_fpga /usr/bin

RUN rm -rf /var/cache/yum/*


