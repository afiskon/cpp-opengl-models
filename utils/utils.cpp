#include "utils.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <sstream>
#include <fstream>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <defer.h>
#include <unistd.h>
#include <stb_image.h>
#include <GL/glext.h>

void flipTexture(unsigned char *textureData, int width, int height, int n) {
  for(int h = 0; h < height/2; ++h) {
    for(int w = 0; w < width; ++w) {
      for(int i = 0; i < n; ++i) {
        int offset1 = (h*width + w)*n + i;
        int offset2 = ((height - h - 1)*width + w)*n + i;
        unsigned char t = textureData[offset1];
        textureData[offset1] = textureData[offset2];
        textureData[offset2] = t;
      }
    }
  }
}

bool loadCommonTexture(char const* fname, GLuint* texturePtr) {
  return loadCommonTextureExt(fname, texturePtr, false);
}

bool loadCommonTextureExt(char const* fname, GLuint* texturePtr, bool flip) {
  *texturePtr = 0;

  int width, height, n;
  unsigned char *textureData = stbi_load(fname, &width, &height, &n, 0);
  if(textureData == nullptr) {
    std::cout << "ERROR: loadCommonTextureExt failed, fname = " << fname << ", stbi_load returned nullptr" << std::endl;
    return false;
  }
  defer(stbi_image_free(textureData));

  if(flip) flipTexture(textureData, width, height, n);

  glGenTextures(1, texturePtr);
  glBindTexture(GL_TEXTURE_2D, *texturePtr);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, textureData);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

  // Generate mipmaps
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glGenerateMipmap(GL_TEXTURE_2D);

  glBindTexture(GL_TEXTURE_2D, 0); // unbind
  return true;
}

#define DDS_HEADER_SIZE 128
#define DDS_SIGNATURE    0x20534444 // "DDS "
#define FORMAT_CODE_DXT1 0x31545844 // "DXT1"
#define FORMAT_CODE_DXT3 0x33545844 // "DXT3"
#define FORMAT_CODE_DXT5 0x35545844 // "DXT5"

bool loadDDSTexture(char const *fname, GLuint *texturePtr) {
  *texturePtr = 0;

  int fd = open(fname, O_RDONLY, 0);
  if(fd < 0) {
    std::cout << "ERROR: loadDDSTexture - open failed, fname =  " << fname <<
      ", " << strerror(errno) << std::endl;
    return false;
  }
  defer(close(fd));

  struct stat st;
  if(fstat(fd, &st) < 0) {
    std::cout << "ERROR: loadDDSTexture - fstat failed, fname = " << fname <<
      ", " << strerror(errno) << std::endl;
    return false;
  }

  size_t fsize = (size_t)st.st_size;
  if(fsize < DDS_HEADER_SIZE) {
    std::cout << "ERROR: loadDDSTexture failed, fname = " << fname <<
      ", fsize = " << fsize << ", less then DDS_HEADER_SIZE (" << DDS_HEADER_SIZE << ")" << std::endl;
    return false;
  }

  unsigned char* dataPtr = (unsigned char*)mmap(nullptr, fsize, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
  if(dataPtr == MAP_FAILED) {
    std::cout << "ERROR: loadDDSTexture - mmap failed, fname = " << fname <<
      ", " << strerror(errno) << std::endl;
    return false;
  }
  defer(munmap(dataPtr, fsize));

  unsigned int signature    = *(unsigned int*)&(dataPtr[ 0]);
  unsigned int height       = *(unsigned int*)&(dataPtr[12]);
  unsigned int width        = *(unsigned int*)&(dataPtr[16]);
  unsigned int mipMapNumber = *(unsigned int*)&(dataPtr[28]);
  unsigned int formatCode   = *(unsigned int*)&(dataPtr[84]);

  if(signature != DDS_SIGNATURE) {
    std::cout << "ERROR: loadDDSTexture failed, fname = " << fname <<
      "invalid signature: 0x" << std::hex << signature << std::endl;
    return false;
  }

  unsigned int format;
  switch(formatCode)
  {
    case FORMAT_CODE_DXT1:
      format = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
      break;
    case FORMAT_CODE_DXT3:
      format = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
      break;
    case FORMAT_CODE_DXT5:
      format = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
      break;
    default:
      std::cout << "ERROR: loadDDSTexture failed, fname = " << fname <<
        ", unknown formatCode: 0x" << std::hex << formatCode << std::endl;
      return false;
  }

  glGenTextures(1, texturePtr);
  glBindTexture(GL_TEXTURE_2D, *texturePtr);
  glPixelStorei(GL_UNPACK_ALIGNMENT,1);

  unsigned int blockSize = (format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT) ? 8 : 16;
  unsigned int offset = DDS_HEADER_SIZE;

  // load mipmaps
  for (unsigned int level = 0; level < mipMapNumber; ++level)
  {
    unsigned int size = ((width+3)/4)*((height+3)/4)*blockSize;
    if(fsize < offset + size) {
      std::cout << "ERROR: loadDDSTexture failed, fname = " << fname <<
        ", fsize = " << fsize << ", level = " << level <<
        ", offset = " << offset << ", size = " << size << std::endl;
      return false;
    }
    glCompressedTexImage2D(GL_TEXTURE_2D, level, format, width, height, 0, size, dataPtr + offset);

    width = width > 1 ? width >> 1 : 1;
    height = height > 1 ? height >> 1 : 1;
    offset += size;
  }

  glBindTexture(GL_TEXTURE_2D, 0); // unbind
  return true;
}

bool checkShaderCompileStatus(GLuint obj) {
  GLint status;
  glGetShaderiv(obj, GL_COMPILE_STATUS, &status);
  if(status == GL_FALSE) {
    GLint length;
    glGetShaderiv(obj, GL_INFO_LOG_LENGTH, &length);
    std::vector<char> log((unsigned long)length);
    glGetShaderInfoLog(obj, length, &length, &log[0]);
    std::cerr << &log[0];
    return true;
  }
  return false;
}

bool checkProgramLinkStatus(GLuint obj) {
  GLint status;
  glGetProgramiv(obj, GL_LINK_STATUS, &status);
  if(status == GL_FALSE) {
    GLint length;
    glGetProgramiv(obj, GL_INFO_LOG_LENGTH, &length);
    std::vector<char> log((unsigned long) length);
    glGetProgramInfoLog(obj, length, &length, &log[0]);
    std::cerr << &log[0];
    return true;
  }
  return false;
}

GLuint loadShader(char const *fname, GLenum shaderType, bool *errorFlagPtr) {
  std::ifstream fileStream(fname);
  std::stringstream buffer;
  buffer << fileStream.rdbuf();
  std::string shaderSource(buffer.str());

  GLuint shaderId = glCreateShader(shaderType);
  const GLchar * const shaderSourceCStr = shaderSource.c_str();

  glShaderSource(shaderId, 1, &shaderSourceCStr, nullptr);
  glCompileShader(shaderId);

  *errorFlagPtr = checkShaderCompileStatus(shaderId);
  if(*errorFlagPtr) {
    glDeleteShader(shaderId);
    return 0;
  }

  return shaderId;
}

GLuint prepareProgram(const std::vector<GLuint>& shaders, bool *errorFlagPtr) {
  *errorFlagPtr = false;

  GLuint programId = glCreateProgram();
  for(auto it = shaders.cbegin(); it != shaders.cend(); ++it) {
    glAttachShader(programId, *it);
  }
  glLinkProgram(programId);

  *errorFlagPtr = checkProgramLinkStatus(programId);
  if(*errorFlagPtr) {
    glDeleteProgram(programId);
    return 0;
  }

  return programId;
}