package main

import (
	"encoding/json"
	//	"fmt"
	log "github.com/Sirupsen/logrus"
	"os"
	"path"
	//	"strconv"
	"strings"
	//	"regexp"
)

var envSwarmGPU *string

const (
	envAccelDevices   = "ACCELERATOR_DEVICES"
	envAccelFunctions = "ACCELERATOR_FUNCTIONS"
)

type acceleratorConfig struct {
	Devices   string
	Functions string
}

type containerConfig struct {
	Pid          int
	Rootfs       string
	Env          map[string]string
	Accelerators *acceleratorConfig
}

// Root contains information about the container's root filesystem on the host.
// github.com/opencontainers/runtime-spec/blob/v1.0.0/specs-go/config.go#L94-L100
type Root struct {
	Path string `json:"path"`
}

// Process contains information to start a specific application inside the container.
// github.com/opencontainers/runtime-spec/blob/v1.0.0/specs-go/config.go#L30-L57
type Process struct {
	Env []string `json:"env,omitempty"`
}

// Spec uses pointers to structs, similarly to the latest version of runtime-spec:
// https://github.com/opencontainers/runtime-spec/blob/v1.0.0/specs-go/config.go#L5-L28
type Spec struct {
	Process *Process `json:"process,omitempty"`
	Root    *Root    `json:"root,omitempty"`
}

// HookState copied from opencontainers/runc
type HookState struct {
	// Version is the version of the specification that is supported.
	Version string `json:"ociVersion"`
	// ID is the container ID
	ID string `json:"id"`
	// Status is the runtime status of the container.
	Status string `json:"status"`
	// Pid is the process ID for the container process.
	Pid int `json:"pid,omitempty"`
	// Bundle is the path to the container's bundle directory.
	Bundle string `json:"bundle"`
	// Annotations are key values associated with the container.
	Annotations map[string]string `json:"annotations,omitempty"`
}

func getEnvMap(e []string) (m map[string]string) {
	m = make(map[string]string)
	for _, s := range e {
		p := strings.SplitN(s, "=", 2)
		if len(p) != 2 {
			logfatal.Fatalln("Process environment map error")
		}
		m[p[0]] = p[1]
	}
	return
}

func loadSpec(path string) (spec *Spec) {
	f, err := os.Open(path)
	if err != nil {
		logfatal.Fatalln("could not open OCI spec:", err)
	}
	defer f.Close()

	if err = json.NewDecoder(f).Decode(&spec); err != nil {
		logfatal.Fatalln("could not decode OCI spec:", err)
	}
	if spec.Process == nil {
		logfatal.Fatalln("Process is empty in OCI spec")
	}
	if spec.Root == nil {
		logfatal.Fatalln("Root is empty in OCI spec")
	}
	return
}

func getAcceleratorConfig(env map[string]string) *acceleratorConfig {
	devices := env[envAccelDevices]
	if (len(devices) == 0) || (devices == "void") || (devices == "none") {
		return nil
	}
	functions := env[envAccelFunctions]
	log.Infof("%s = %s, %s = %s", envAccelDevices, devices, envAccelFunctions, functions)

	return &acceleratorConfig{
		Devices:   devices,
		Functions: functions,
	}
}

func getContainerConfig() (config containerConfig) {
	var h HookState

	// note: runc passes as argument HookState containing bundle info
	d := json.NewDecoder(os.Stdin)
	if err := d.Decode(&h); err != nil {
		logfatal.Fatalln("could not decode container state:", err)
	}
	b := h.Bundle
	log.Infof("Container Bundle [%s]", b)

	s := loadSpec(path.Join(b, "config.json"))

	env := getEnvMap(s.Process.Env)

	return containerConfig{
		Pid:          h.Pid,
		Rootfs:       s.Root.Path,
		Env:          env,
		Accelerators: getAcceleratorConfig(env),
	}
}
