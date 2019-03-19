package main

import (
	"flag"
	"fmt"
	log "github.com/Sirupsen/logrus"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"syscall"
)

const (
	acceleratorTool = "accelerator-container-runtime-tool"
	syslogFile      = "/var/log/accelerator-runtime-hook.log"
)

var (
	debugflag = flag.Bool("debug", false, "enable debug output")
	logfatal  *log.Logger
)

func loginit() {
	f, err := os.OpenFile(syslogFile, os.O_WRONLY|os.O_APPEND|os.O_CREATE, 0644)
	Formatter := new(log.TextFormatter)
	// You can change the Timestamp format. But you have to use the same date and time.
	// "2006-01-02 15:04:05" Works. If you change any digit, it won't work
	// ie "Mon Jan 2 15:04:05 MST 2006" is the reference time. You can't change it
	Formatter.TimestampFormat = "02-01-2006 15:04:05"
	Formatter.FullTimestamp = true
	log.SetFormatter(Formatter)
	if err != nil {
		// Cannot open log file. Logging to stderr
		log.Errorln("Log to file failed:", err)
	} else {
		log.SetOutput(f)
	}
	if *debugflag {
		// note: settable only on cmd line, not from docker
		log.SetLevel(log.DebugLevel)
	} else {
		log.SetLevel(log.InfoLevel)
	}
	// Fatal errors are logged to stderr, followed by exit()
	logfatal = log.New()
}

func usage() {
	fmt.Fprintf(os.Stderr, "Usage of %s:\n", os.Args[0])
	flag.PrintDefaults()
	fmt.Fprintf(os.Stderr, "\nCommands:\n")
	fmt.Fprintf(os.Stderr, "  prestart\n        run the prestart hook\n")
	fmt.Fprintf(os.Stderr, "  poststart\n       nothing to do\n")
	fmt.Fprintf(os.Stderr, "  poststop\n        nothing to do\n")
}

// getRootfsPath returns an absolute path. We don't need to resolve symlinks for now.
func getRootfsPath(config containerConfig) string {
	rootfs, err := filepath.Abs(config.Rootfs)
	if err != nil {
		logfatal.Fatalln("rootfs invalid:", err)
	}
	return rootfs
}

func doPrestart() {

	container := getContainerConfig()
	//log.Infof("Container has PID %d, Rootfs [%s]", container.Pid, container.Rootfs)

	if container.Accelerators == nil {
		// Not an accelerator container
		return
	}

	path, err := exec.LookPath(acceleratorTool)
	if err != nil {
		logfatal.Fatalln("exec failed:", acceleratorTool, "not found")
	}
	args := []string{path}

	args = append(args, fmt.Sprintf("--devices=%s", container.Accelerators.Devices))
	if len(container.Accelerators.Functions) > 0 {
		args = append(args, fmt.Sprintf("--functions=%s", container.Accelerators.Functions))
	}
	args = append(args, fmt.Sprintf("--pid=%s", strconv.FormatUint(uint64(container.Pid), 10)))
	args = append(args, fmt.Sprintf("--rootfs=%s", getRootfsPath(container)))
	args = append(args, fmt.Sprintf("--log=%s", syslogFile))
	if *debugflag {
		args = append(args, "--loglevel=7") // debug
	} else {
		args = append(args, "--loglevel=6") // info
	}

	args = append(args, "configure")

	//	if cli.LoadKmods {
	//		args = append(args, "--load-kmods")
	//	}

	log.Infof("exec command: %v", args)

	err = syscall.Exec(args[0], args, os.Environ())
	logfatal.Fatalln("exec failed:", err)
}

func main() {
	flag.Usage = usage
	flag.Parse()

	loginit()

	args := flag.Args()
	if len(args) == 0 {
		flag.Usage()
		os.Exit(2)
	}

	switch args[0] {
	case "prestart":
		doPrestart()
		os.Exit(0)
	case "poststart":
		fallthrough
	case "poststop":
		os.Exit(0)
	default:
		flag.Usage()
		os.Exit(2)
	}

}
