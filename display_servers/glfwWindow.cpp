/* 
   Copyright (c) 2016-2017 Ingo Wald

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
*/

#include "glfwWindow.h"
#include <chrono>

namespace ospray {
  namespace dw {

    using std::endl;
    using std::cout;

    GLFWindow::GLFWindow(const vec2i &size,
                         const vec2i &position,
                         const std::string &title,
                         bool doFullScreen, 
                         bool stereo)
      : size(size),
        position(position),
        title(title),
        leftEye(NULL),
        rightEye(NULL),
        stereo(stereo),
        receivedFrameID(-1),
        displayedFrameID(-1),
        doFullScreen(doFullScreen)
    {
      create();
    }


    void GLFWindow::create()
    {
      glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
      glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
      
      if (doFullScreen) {
        auto *monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        size = getScreenSize();
        glfwWindowHint(GLFW_AUTO_ICONIFY,false);
        glfwWindowHint(GLFW_RED_BITS, mode->redBits);
        glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
        glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
        glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
        
        this->handle = glfwCreateWindow(mode->width, mode->height,
                                  title.c_str(), monitor, nullptr);
      } else {
        glfwWindowHint(GLFW_DECORATED, 0);
        this->handle = glfwCreateWindow(size.x,size.y,title.c_str(),
                                  NULL,NULL);
        glfwSetWindowPos(this -> handle, position.x, position.y);

      }

      glfwMakeContextCurrent(this->handle);
    }

    void GLFWindow::setFrameBuffer(const uint32_t *leftEye, const uint32 *rightEye)
    {
      {
        std::lock_guard<std::mutex> lock(this->mutex);
        this->leftEye = leftEye;
        this->rightEye = rightEye;
        receivedFrameID++;
        newFrameAvail.notify_one();
        // vec2i img_size{512, 512};
        // writePPM("test_left.ppm", &img_size, leftEye);
        // std::cout << "Image saved to 'test_left.ppm'\n";
      }
    }

    void GLFWindow::display() 
    {
      {
        std::unique_lock<std::mutex> lock(mutex);
        bool gotNewFrame 
          = newFrameAvail.wait_for(lock,std::chrono::milliseconds(100),
                                   [this](){return receivedFrameID > displayedFrameID; });
        // glfwShowWindow(window);

        if (!gotNewFrame)
          return;
        
        // std::cout << "new frame " << std::endl;

        vec2i currentSize(0);
        glfwGetFramebufferSize(this->handle, &currentSize.x, &currentSize.y);

        glViewport(0, 0, currentSize.x, currentSize.y);
        glClear(GL_COLOR_BUFFER_BIT);


        if (!leftEye) {
          /* invalid ... */
        } else {
          assert(rightEye == NULL);
          // no stereo
          // glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
          glDrawPixels(size.x, size.y, GL_RGBA, GL_UNSIGNED_BYTE, leftEye);
        }
      
        glfwSwapBuffers(this->handle);
      }
      {
        std::lock_guard<std::mutex> lock(mutex);
        displayedFrameID++;
        newFrameDisplayed.notify_one();
      }
    }

    vec2i GLFWindow::getSize() const 
    { 
      return size; 
    }

    bool GLFWindow::doesStereo() const
    { 
      return stereo; 
    }
    
    void GLFWindow::run() 
    { 
      while (!glfwWindowShouldClose(this->handle)) {
        // double lastTime = getSysTime();
        glfwPollEvents();
        display();
        // double thisTime = getSysTime();
      }
    }
    
  } // ::ospray::dw
} // ::ospray
