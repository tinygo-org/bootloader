package main

import (
	"machine"
	"time"

	"github.com/tinygo-org/bluetooth"
	"github.com/tinygo-org/bootloader/dfuservice"
)

func main() {
	println("start")

	// Make sure the Bluetooth stack is initialized.
	adapter := bluetooth.DefaultAdapter
	adapter.Enable()

	// Add the DFU service.
	dfuservice.AddService(adapter)

	// Make sure we can be found.
	adv := adapter.DefaultAdvertisement()
	adv.Configure(bluetooth.AdvertisementOptions{
		LocalName:    "blink",
		ServiceUUIDs: []bluetooth.UUID{dfuservice.ServiceUUID},
	})
	adv.Start()

	// Regular LED blinking code.
	led := machine.LED
	led.Configure(machine.PinConfig{Mode: machine.PinOutput})
	for {
		led.Low()
		time.Sleep(time.Millisecond * 500)

		led.High()
		time.Sleep(time.Millisecond * 500)
	}
}
