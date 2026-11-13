#include "smokeviz/Screenshot.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

// stb_image_write is part of the stb package the Renderer already downloads (it uses stb_image for loading);
// the Renderer does not compile the writer, so its implementation lives in this one file.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstddef>
#include <vector>

namespace smokeviz {

bool SaveWindowPng(const std::string& path) {
    GLFWwindow* window = glfwGetCurrentContext();
    if (window == nullptr) return false;
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (width <= 0 || height <= 0) return false;

    GLint previousFbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousFbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_FRONT);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 3);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadBuffer(GL_BACK);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousFbo));

    // OpenGL rows go bottom-up, image files top-down.
    stbi_flip_vertically_on_write(1);
    return stbi_write_png(path.c_str(), width, height, 3, pixels.data(), width * 3) != 0;
}

} // namespace smokeviz
