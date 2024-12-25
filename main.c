#include <stdio.h>
#include <VideoToolbox/VideoToolbox.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>
#include <pthread.h>

// NAL unit types
#define NAL_SPS 0x07
#define NAL_PPS 0x08

// Frames will be coming asynchronously, so we need to synchronize access to the file
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

// Utility function to check if a given byte sequence is a NAL start code
bool isNalStartCode(uint8_t *data, size_t size) {
  if (size < 4) {
    return false;
  }
  return (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x01);
}

// Callback function that gets called when a frame is decoded
void decompressionOutputCallback(void *decompressionOutputRefCon, void *sourceFrameRefCon, OSStatus status, VTDecodeInfoFlags infoFlags, CVImageBufferRef imageBuffer, CMTime presentationTimeStamp, CMTime presentationDuration) {
  if (status != noErr) {
    printf("Error decoding frame, code: %d\n", (int)status);
    return;
  }

  pthread_mutex_lock(&file_mutex);
  CVPixelBufferLockBaseAddress(imageBuffer, 0);

  // Get the YUV data from the image buffer
  uint8_t *baseAddressY = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(imageBuffer, 0);
  uint8_t *baseAddressUV = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(imageBuffer, 1);
  size_t bytesPerRowY = CVPixelBufferGetBytesPerRowOfPlane(imageBuffer, 0);
  size_t bytesPerRowUV = CVPixelBufferGetBytesPerRowOfPlane(imageBuffer, 1);
  size_t width = CVPixelBufferGetWidth(imageBuffer);
  size_t height = CVPixelBufferGetHeight(imageBuffer);

  // Dump the frame to disk
  FILE *file = fopen("output.raw", "ab");
  if (file != NULL) {
    fwrite(baseAddressY, sizeof(uint8_t), bytesPerRowY * height, file);
    fwrite(baseAddressUV, sizeof(uint8_t), bytesPerRowUV * height / 2, file);
    fclose(file);
    printf("Frame dumped to disk\n");
  } else {
    printf("Failed to open output file for writing\n");
  }

  // Cleanup
  CVPixelBufferUnlockBaseAddress(imageBuffer, 0);
  pthread_mutex_unlock(&file_mutex);
}

int main() {
  // Open the file
  const char *filename = "video.h264";
  FILE *file = fopen(filename, "rb");
  if (!file) {
    printf("Failed to open file: %s\n", filename);
    return 0;
  }

  // Read the entire file into a buffer
  fseek(file, 0, SEEK_END);
  long fileSize = ftell(file);
  fseek(file, 0, SEEK_SET);
  uint8_t *buffer = (uint8_t *)malloc(fileSize);
  if (!buffer) {
    printf("Failed to allocate memory\n");
    fclose(file);
    return 0;
  }
  fread(buffer, 1, fileSize, file);
  fclose(file); // Close the file after reading


  size_t i = 0;
  uint8_t *spsData = NULL;
  uint8_t *ppsData = NULL;
  size_t spsSize = 0;
  size_t ppsSize = 0;
  bool foundSpsPps = false;

  while (i < fileSize) {
    // Find the next NAL start code
    if (isNalStartCode(&buffer[i], fileSize - i)) {
      // Skip the start code (0x00000001)
      // Now, 'i' points to the start of the NAL unit
      // Find the length of the NAL unit
      i += 4;
      size_t nalStart = i;
      while (i < fileSize && !(isNalStartCode(&buffer[i], fileSize - i))) {
        i++;
      }

      size_t nalLength = i - nalStart;
      printf("Found NAL unit with size: %zu bytes\n", nalLength);

      // Get the NAL unit
      uint8_t *nalUnit = &buffer[nalStart];

      // Get SPS and PPS data
      if ((nalUnit[0] & 0x1F) == NAL_SPS) {
        spsData = nalUnit;
        spsSize = nalLength;
        printf("SPS NAL unit found, size: %zu bytes\n", spsSize);
      } else if ((nalUnit[0] & 0x1F) == NAL_PPS) {
        ppsData = nalUnit;
        ppsSize = nalLength;
        printf("PPS NAL unit found, size: %zu bytes\n", ppsSize);
      }

      // If both SPS and PPS are found, set up the decoder
      if (spsData != NULL && ppsData != NULL) {
        foundSpsPps = true;
      }

      // Store the frame data in a buffer that can be used later for decoding
      uint8_t *frameData = (uint8_t *)malloc(nalLength);
      if (!frameData) {
        printf("Failed to allocate memory for frame data\n");
        free(buffer);
        fclose(file);
        return 0;
      }
      memcpy(frameData, nalUnit, nalLength);
      // Here you can store frameData in a list or queue for later use
      // For simplicity, we will just free it immediately
      free(frameData);

      // Move to the next NAL unit
    } else {
      i++; // If not a start code, just move to the next byte
    }
  }

  if (!foundSpsPps) {
    printf("Failed to find SPS and PPS\n");
    free(buffer);
    fclose(file);
    return 0;
  }

  // Create a CMVideoFormatDescriptionRef from the SPS and PPS data
  CMVideoFormatDescriptionRef formatDescription = NULL;
  const uint8_t *parameterSetPointers[2] = {spsData, ppsData};
  const size_t parameterSetSizes[2] = {spsSize, ppsSize};
  OSStatus status = CMVideoFormatDescriptionCreateFromH264ParameterSets(kCFAllocatorDefault, 2, parameterSetPointers, parameterSetSizes, 4, &formatDescription);
  if (status != noErr) {
    printf("Failed to create format description\n");
    free(buffer);
    fclose(file);
    return 0;
  }

  // Get dimensions of the video
  CMVideoDimensions videoDimensions = CMVideoFormatDescriptionGetDimensions(formatDescription);
  printf("Video dimensions: %d x %d\n", videoDimensions.width, videoDimensions.height);
  CFDictionaryRef dictionary = CVPixelFormatDescriptionCreateWithPixelFormatType(kCFAllocatorDefault, kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange);
  if (dictionary == NULL) {
    printf("Failed to create dictionary\n");
    free(buffer);
    fclose(file);
    return 0;
  }

  // Create a H264 decoder session with a callback and specify YUV420 pixel format
  VTDecompressionSessionRef decompressionSession = NULL;
  VTDecompressionOutputCallbackRecord outputCallback;

  outputCallback.decompressionOutputCallback = decompressionOutputCallback;
  outputCallback.decompressionOutputRefCon = NULL;
  VTDecompressionSessionCreate(kCFAllocatorDefault, formatDescription, dictionary, NULL, &outputCallback, &decompressionSession);
  if (status != noErr) {
    printf("Failed to create decompression session\n");
    free(buffer);
    fclose(file);
    return 0;
  }

  // Read the file again but this time, decode the frames
  i = 0;
  while (i < fileSize) {
    if (isNalStartCode(&buffer[i], fileSize - i)) {
      size_t nalStart = i;
      i += 4;
      while (i < fileSize && !(isNalStartCode(&buffer[i], fileSize - i))) {
        i++;
      }
      size_t nalLength = i - nalStart;
      uint8_t *nalUnit = &buffer[nalStart];

      // Create a CMBlockBufferRef from the NAL unit
      CMBlockBufferRef blockBuffer = NULL;
      OSStatus status = CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, nalUnit, nalLength, kCFAllocatorNull, NULL, 0, nalLength, 0, &blockBuffer);
      if (status != noErr) {
        printf("Failed to create block buffer\n");
        free(buffer);
        fclose(file);
        return 0;
      }

      // Replace the NAL start code with the length of the NAL unit
      uint32_t nalLength32 = htonl(nalLength - 4);
      status = CMBlockBufferReplaceDataBytes(&nalLength32, blockBuffer, 0, 4);
      if (status != noErr) {
        printf("Failed to replace data bytes\n");
        free(buffer);
        fclose(file);
        return 0;
      }


      // Create a CMSampleBufferRef from the block buffer
      CMSampleBufferRef sampleBuffer = NULL;
      const size_t sampleSizeArray[] = {nalLength};
      status = CMSampleBufferCreate(kCFAllocatorDefault, blockBuffer, true, NULL, NULL, formatDescription, 1, 0, NULL, 1, sampleSizeArray, &sampleBuffer);
      if (status != noErr) {
        printf("Failed to create sample buffer\n");
        free(buffer);
        fclose(file);
        return 0;
      }

      // Decode the sample buffer
      VTDecodeFrameFlags flags = 0;
      VTDecodeInfoFlags infoFlags = 0;
      status = VTDecompressionSessionDecodeFrame(decompressionSession, sampleBuffer, flags, NULL, &infoFlags);
      if (status != noErr) {
        printf("Failed to decode frame\n");
        free(buffer);
        fclose(file);
        return 0;
      }

      // Release the block buffer and sample buffer
      CFRelease(blockBuffer);
      CFRelease(sampleBuffer);
    } else {
      i++;
    }
  }

  // Goodbye!
  free(buffer);
  fclose(file);
  return 0;
}
