
DOCKER ?= docker

VERSION := 1.0.0
PKG_REV := 0
GOLANG_VERSION := 1.9.4

#DOCKER_VERSION := 17.12.1
#DOCKER_PATCH := 9f9c96235cc97674e935002fc3d78361b696a69e
DOCKER_VERSION := 17.03.2
DOCKER_PATCH := 54296cf40ad8143b62dbcaa1d90e520a2136ddfe

DIST_DIR  := $(CURDIR)/dist

.NOTPARALLEL:
.PHONY: all

#all: ubuntu16.04 ubuntu14.04 debian9 debian8 centos7 amzn2 amzn1
all: cleantool centos7 ubuntu16.04 debian9

cleanall: cleantool
	sudo rm -rf dist/centos* dist/ubuntu* dist/amzn* dist/debian*

cleantool:
	cd runtime-tool && make clean

centos%: $(CURDIR)/dist/Dockerfile.centos
	$(DOCKER) build --build-arg VERSION_ID="$*" \
	                --build-arg GOLANG_VERSION="$(GOLANG_VERSION)" \
	                --build-arg PKG_VERS="$(VERSION)+docker$(DOCKER_VERSION)" \
	                --build-arg PKG_REV="$(PKG_REV)" \
	                --build-arg RUNC_COMMIT="$(DOCKER_PATCH)" \
	                -t "accelerator-container:centos$*" -f dist/Dockerfile.centos .
	$(DOCKER) run --rm -v $(DIST_DIR)/$@:/dist:Z "accelerator-container:centos$*"
	$(DOCKER) run --rm -v `pwd`:/dist alpine chown -R `id -u`:`id -g` /dist

ubuntu%: $(CURDIR)/dist/Dockerfile.ubuntu
	$(DOCKER) build --build-arg VERSION_ID="$*" \
	                --build-arg GOLANG_VERSION="$(GOLANG_VERSION)" \
	                --build-arg PKG_VERS="$(VERSION)+docker$(DOCKER_VERSION)" \
	                --build-arg PKG_REV="$(PKG_REV)" \
	                --build-arg RUNC_COMMIT="$(DOCKER_PATCH)" \
	                -t "accelerator-container:ubuntu$*" -f dist/Dockerfile.ubuntu .
	$(DOCKER) run --rm -v $(DIST_DIR)/$@:/dist:Z "accelerator-container:ubuntu$*"
	$(DOCKER) run --rm -v `pwd`:/dist alpine chown -R `id -u`:`id -g` /dist

amzn%: $(CURDIR)/dist/Dockerfile.amzn
	$(DOCKER) build --build-arg VERSION_ID="$*" \
	                --build-arg GOLANG_VERSION="$(GOLANG_VERSION)" \
	                --build-arg PKG_VERS="$(VERSION)+docker$(DOCKER_VERSION)" \
	                --build-arg PKG_REV="$(PKG_REV).amzn$*" \
	                --build-arg RUNC_COMMIT="$(DOCKER_PATCH)" \
	                -t "accelerator-container:amzn$*" -f dist/Dockerfile.amzn .
	$(DOCKER) run --rm -v $(DIST_DIR)/$@:/dist:Z "accelerator-container:amzn$*"
	$(DOCKER) run --rm -v `pwd`:/dist alpine chown -R `id -u`:`id -g` /dist

debian%: $(CURDIR)/dist/Dockerfile.debian
	$(DOCKER) build --build-arg VERSION_ID="$*" \
	                --build-arg GOLANG_VERSION="$(GOLANG_VERSION)" \
	                --build-arg PKG_VERS="$(VERSION)+docker$(DOCKER_VERSION)" \
	                --build-arg PKG_REV="$(PKG_REV)" \
	                --build-arg RUNC_COMMIT="$(DOCKER_PATCH)" \
	                -t "accelerator-container:debian$*" -f dist/Dockerfile.debian .
	$(DOCKER) run --rm -v $(DIST_DIR)/$@:/dist:Z "accelerator-container:debian$*"
	$(DOCKER) run --rm -v `pwd`:/dist alpine chown -R `id -u`:`id -g` /dist
