package main

import (
	"bytes"
	"encoding/binary"
	"flag"
	"fmt"
	"os"
	"time"

	"github.com/tinygo-org/bluetooth"
)

var adapter = bluetooth.DefaultAdapter

var (
	serviceUUID = bluetooth.NewUUID([16]byte{0xcb, 0x15, 0x00, 0x01, 0x24, 0x04, 0x4e, 0x66, 0xab, 0x07, 0xa5, 0xf1, 0x05, 0x3f, 0x14, 0xce})
	commandUUID = bluetooth.NewUUID([16]byte{0xcb, 0x15, 0x00, 0x02, 0x24, 0x04, 0x4e, 0x66, 0xab, 0x07, 0xa5, 0xf1, 0x05, 0x3f, 0x14, 0xce})
	dataUUID    = bluetooth.NewUUID([16]byte{0xcb, 0x15, 0x00, 0x03, 0x24, 0x04, 0x4e, 0x66, 0xab, 0x07, 0xa5, 0xf1, 0x05, 0x3f, 0x14, 0xce})
)

// Commands used, from the client (this program) to the server.
const (
	commandResetBootloader = 0x00
	commandReset           = 0x01
	commandStart           = 0x02 // start, will earse the necessary flash area
)

// Statuses returned. They can be returned at any time, but are usually returned
// as a response of a completed event.
const (
	statusPong               = 0x01 // ping reply
	statusEraseStarted       = 0x02 // erase started
	statusEraseFinished      = 0x03 // erase finished, client may start to stream data
	statusWriteFinished      = 0x04 // write finished, firmware has been rewritten
	statusBusy               = 0x10 // another command is still running
	statusInvalidEraseStart  = 0x20 // invalid start address for erase command (not at APP_CODE_BASE)
	statusInvalidEraseLength = 0x21 // invalid length for erase command (would overwrite bootloader)
	statusEraseFailed        = 0x30 // could not erase flash page
	statusWriteFailed        = 0x31 // could not write flash page
	statusWriteTooFast       = 0x32 // could not write flash page: data came in faster than could be written
)

func usage() {
	fmt.Printf("usage: %s <filename>\n", os.Args[0])
	os.Exit(0)
}

func main() {
	flag.Parse()
	if flag.NArg() != 1 {
		usage()
	}

	startAddr, data, err := readInput(flag.Arg(0))
	handleError("could not read input file", err)
	if startAddr+uint64(len(data)) > 0xffffffff {
		fmt.Fprintf(os.Stderr, "file data does not fit (range: 0x%08x..0x%08x)\n", startAddr, startAddr+uint64(len(data)))
	}

	err = adapter.Enable()
	handleError("could not enable BLE adapter", err)

	var foundDevice bluetooth.ScanResult
	fmt.Println("Looking for nearby device...")
	err = adapter.Scan(func(adapter *bluetooth.Adapter, result bluetooth.ScanResult) {
		if !result.AdvertisementPayload.HasServiceUUID(serviceUUID) {
			return
		}
		foundDevice = result

		// Stop the scan.
		err := adapter.StopScan()
		handleError("could not stop the scan", err)
	})
	handleError("could not start a scan", err)

	// Print the device we've found.
	if name := foundDevice.LocalName(); name == "" {
		fmt.Printf("Connecting to %s...\n", foundDevice.Address)
	} else {
		fmt.Printf("Connecting to %s (%s)...\n", name, foundDevice.Address)
	}

	// Connect to it.
	device, err := adapter.Connect(foundDevice.Address, bluetooth.ConnectionParams{})
	handleError("failed to connect", err)

	// Connected. Look up the DFU service.
	fmt.Println("Looking up DFU service...")
	services, err := device.DiscoverServices([]bluetooth.UUID{serviceUUID})
	handleError("failed to discover the DFU service", err)
	service := services[0]

	// Get the two characteristics present in this service.
	chars, err := service.DiscoverCharacteristics([]bluetooth.UUID{commandUUID, dataUUID})
	handleError("failed to discover characteristics", err)
	commandChar := chars[0]
	dataChar := chars[1]

	// Subscribe to status updates (command accepted, command rejected, command
	// completed).
	responseChan := make(chan uint8)
	commandChar.EnableNotifications(func(buf []byte) {
		responseChan <- buf[0]
	})

	// Send the "reset into bootloader" message. It is ignored by the bootloader
	// but results in a reset in the stub DFU service.
	// We normally don't get an error, but will get the error with the next
	// command we'll send.
	_, err = commandChar.WriteWithoutResponse([]byte{commandResetBootloader})
	handleError("failed to send reset bootloader command", err)

	// Start the write by erasing the flash.
	buf := &bytes.Buffer{}
	buf.Write([]byte{commandStart, 0, 0, 0})
	binary.Write(buf, binary.LittleEndian, uint32(startAddr))
	binary.Write(buf, binary.LittleEndian, uint32(len(data)))

	_, err = commandChar.WriteWithoutResponse(buf.Bytes())
	if err != nil && err.Error() == "Not connected" {
		// The device reset itself, so the connection will have been broken.
		// Re-establish the connection.
		fmt.Println("Lost connection. This probably means the device is resetting into DFU mode. Finding device again...")
		err = adapter.Scan(func(adapter *bluetooth.Adapter, result bluetooth.ScanResult) {
			if result.Address != foundDevice.Address {
				return
			}
			foundDevice = result

			// Stop the scan.
			err := adapter.StopScan()
			handleError("could not stop the scan", err)
		})
		handleError("could not start a scan", err)

		// Connect to it.
		fmt.Printf("Reconnecting...\n")
		device, err := adapter.Connect(foundDevice.Address, bluetooth.ConnectionParams{})
		handleError("failed to connect", err)

		// Connected. Look up the DFU service.
		fmt.Println("Looking up DFU service...")
		services, err := device.DiscoverServices([]bluetooth.UUID{serviceUUID})
		handleError("failed to discover the DFU service", err)
		service := services[0]

		// Get the two characteristics present in this service.
		chars, err := service.DiscoverCharacteristics([]bluetooth.UUID{commandUUID, dataUUID})
		handleError("failed to discover characteristics", err)
		commandChar = chars[0]
		dataChar = chars[1]

		// Try again to erase the flash.
		_, err = commandChar.WriteWithoutResponse(buf.Bytes())
	}

	fmt.Printf("Erasing flash (start 0x%x, length %d bytes or %.1fkB)...\n", startAddr, len(data), float64(len(data))/1024)
	handleError("failed to send erase command", err)

	// Wait until the command is accepted.
	status := <-responseChan

	if status == statusEraseStarted {
		// Wait until the command is completed.
		status = <-responseChan
	}
	switch status {
	case statusEraseFinished:
		// Finished!
		err = nil
	case statusInvalidEraseStart:
		err = fmt.Errorf("invalid start address: 0x%x", startAddr)
	case statusInvalidEraseLength:
		err = fmt.Errorf("invalid length: 0x%x", len(data))
	case statusBusy:
		err = fmt.Errorf("an operation is already in progress")
	case statusEraseFailed:
		err = fmt.Errorf("failed to erase flash")
	default:
		err = fmt.Errorf("unknown error (0x%x)", status)
	}
	handleError("could not erase flash", err)

	// Write application data.
	startWrite := time.Now()
	for i := 0; i < len(data); i += 20 {
		fmt.Printf("\rWriting 0x%x (%d%%)...", startAddr+uint64(i), i*100/len(data))
		dataChar.WriteWithoutResponse(data[i : i+20])
	}

	// Wait for confirmation everything has been written.
	status = <-responseChan
	writeDuration := time.Since(startWrite)
	fmt.Print("\033[2K\r")
	if status == statusWriteFinished {
		err = nil // write finished
	} else if status == statusWriteFailed {
		err = fmt.Errorf("write failed")
	} else if status == statusWriteTooFast {
		err = fmt.Errorf("write was too fast") // should not happen in practice
	} else {
		err = fmt.Errorf("unknown (code 0x%x)", status)
	}
	handleError("failed to write new application", err)

	// Completed.
	fmt.Printf("Write completed in %s (%.1f kB/s).\n", writeDuration.Round(time.Millisecond), float64(len(data))/1000/writeDuration.Seconds())
	fmt.Printf("Resetting device...\n")
	commandChar.WriteWithoutResponse([]byte{commandReset})
}

func handleError(msg string, err error) {
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s: %s\n", msg, err)
		os.Exit(1)
	}
}
