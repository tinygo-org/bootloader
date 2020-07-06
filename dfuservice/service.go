// Package dfuservice implements the DFU service for the TinyGo bootloader. It
// presents a stub service that allows resetting a device to enter DFU mode in
// the bootloader.
package dfuservice

import (
	"device/arm"
	"device/nrf"

	"github.com/tinygo-org/bluetooth"
)

// ServiceUUID is the DFU service UUID, which should be present in the
// advertisement as a service.
var ServiceUUID = bluetooth.NewUUID([16]byte{0xcb, 0x15, 0x00, 0x01, 0x24, 0x04, 0x4e, 0x66, 0xab, 0x07, 0xa5, 0xf1, 0x05, 0x3f, 0x14, 0xce})

var (
	commandUUID = bluetooth.NewUUID([16]byte{0xcb, 0x15, 0x00, 0x02, 0x24, 0x04, 0x4e, 0x66, 0xab, 0x07, 0xa5, 0xf1, 0x05, 0x3f, 0x14, 0xce})
	dataUUID    = bluetooth.NewUUID([16]byte{0xcb, 0x15, 0x00, 0x03, 0x24, 0x04, 0x4e, 0x66, 0xab, 0x07, 0xa5, 0xf1, 0x05, 0x3f, 0x14, 0xce})
)

// AddService adds the stub DFU service to the list of services. To make use of
// this service, it also needs to be advertised in the BLE advertisement packet.
// See the blink example for how you can do that, it only takes a few lines of
// code to add DFU support to an application.
func AddService(adapter *bluetooth.Adapter) error {
	return adapter.AddService(&bluetooth.Service{
		UUID: ServiceUUID,
		Characteristics: []bluetooth.CharacteristicConfig{
			{
				UUID:  commandUUID,
				Flags: bluetooth.CharacteristicWritePermission | bluetooth.CharacteristicNotifyPermission,
				WriteEvent: func(client bluetooth.Connection, offset int, value []byte) {
					if offset == 0 && len(value) == 1 && value[0] == 0 {
						// This is a commandResetBootloader, so enter the the
						// bootloader.

						// Disable the SoftDevice before reset, otherwise GPREGRET is not available.
						// The SVCall number 0x11 means SD_SOFTDEVICE_DISABLE in all SoftDevice
						// versions I checked (s110v8, s132v6, s140v7), so is likely to remain
						// constant.
						arm.SVCall0(0x11) // SD_SOFTDEVICE_DISABLE

						// Set the low bit of GPREGRET, so that the bootloader will enter DFU mode
						// instead of starting the application as usual.
						nrf.POWER.GPREGRET.Set(1)

						// Reset the system.
						arm.SystemReset()

						// This should be unreachable.
					}
				},
			},
			{
				// The data characteristic. It is included here to work around
				// caching issues in BlueZ. BlueZ doesn't always clear the
				// discovered service cache, even after removing the device. I
				// had to rm -rf /var/lib/bluetooth/ to get it to work again.
				// Therefore, include it here to work around that.
				UUID:  dataUUID,
				Flags: bluetooth.CharacteristicWriteWithoutResponsePermission,
			},
		},
	})
}
