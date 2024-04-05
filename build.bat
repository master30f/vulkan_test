@echo off

C:\VulkanSDK\1.3.280.0\Bin\glslc.exe .\shaders\shader.vert -o .\shaders\vert.spv
C:\VulkanSDK\1.3.280.0\Bin\glslc.exe .\shaders\shader.frag -o .\shaders\frag.spv
gcc main.c C:\glfw3\lib-mingw-w64\libglfw3.a -DDEBUG -IC:\glfw3\include\GLFW -IC:\VulkanSDK\1.3.280.0\Include -I.\lib -I.\lib\cglm\include -LC:\VulkanSDK\1.3.280.0\Lib -lvulkan-1 -lgdi32 -Wall -Wextra -o main