# Audio Files

This folder contains audio files used by the Money Eyes Robot project.

## Files

### cash_44_stereo.wav
- **Format**: 44.1kHz, 16-bit, Stereo WAV
- **Duration**: ~2-3 seconds
- **Usage**: Plays when eyes transform to $ symbols
- **Source**: [OtoLogic](https://otologic.jp/)
- **License**: Free for commercial and non-commercial use
- **License URL**: https://otologic.jp/free/license.html
- **Description**: Cash register sound effect

## Installation

1. Copy `cash_44_stereo.wav` to the **root directory** of your SD card
2. Insert SD card into M5Stack
3. The file will be automatically detected and played

## File Requirements

- **Sample Rate**: 44.1kHz (CD quality)
- **Bit Depth**: 16-bit
- **Channels**: Stereo (2 channels)
- **Format**: Uncompressed WAV
- **Size**: Should be under 5MB for best performance

## Custom Audio

You can replace `cash_44_stereo.wav` with your own audio file:

1. Ensure it meets the format requirements above
2. Keep the filename as `cash_44_stereo.wav`
3. Place it on SD card root directory

## Notes

- Larger files may cause longer loading times
- Audio quality affects playback smoothness
- Consider file size when using battery power