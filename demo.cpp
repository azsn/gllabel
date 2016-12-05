/*
 * Aidan Shafran <zelbrium@gmail.com>, 2016.
 * 
 * Demo code for GLLabel. Depends on GLFW3, GLEW, GLM, FreeType2, and C++11.
 * Makefile was created for use on Arch Linux. I haven't tested it elsewhere.
 */

#include "label.hpp"
#include <glfw3.h>
#include <glm/gtx/transform.hpp>
#include <locale>
#include <codecvt>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <fstream>

static const uint32_t kWidth = 1536;
static const uint32_t kHeight = 1152;

static GLLabel *Label;
static bool spin = false;
static FT_Face defaultFace;
static FT_Face boldFace;
static bool fuzz = true;
static bool regular = true;
float horizontalTransform = 0.0;
float verticalTransform = 0.8;
float scale = 1;

void onKeyPress(GLFWwindow *window, int key, int scanCode, int action, int mods);
void onCharTyped(GLFWwindow *window, unsigned int codePoint, int mods);
void onScroll(GLFWwindow *window, double deltaX, double deltaY);
std::u32string toUTF32(const std::string &s);
static GLuint loadShaderProgramFile(const char *vertexShaderPath, const char *fragmentShaderPath);

struct CPoint
{
	glm::vec3 coordinate;
	glm::vec3 color;
	glm::vec2 uv;
};

static void setTextFile(std::string path)
{
	std::ifstream file(path);
	std::string str;
	std::string file_contents;
	while (std::getline(file, str))
	{
		file_contents += str;
		file_contents.push_back('\n');
	}
	
	Label->SetText(toUTF32(file_contents), 1, glm::vec4(0,0,0,1), defaultFace);
	Label->SetCaretPosition(Label->GetText().size());
}

static void loadNextText()
{
	verticalTransform = 0.8;
	horizontalTransform = 0;
	static int i = 0;
	scale = 1;
	switch(i)
	{
	default:
		i=0;
	case 0:
		Label->SetText(U"Welcome to vector-based GPU text rendering!\nType whatver you want!\n\nPress LEFT/RIGHT to move cursor.\nPress ESC to toggle rotate.\nPress TAB to change files.\nPress UP to enable warps.\nPress DOWN to switch warps.\nPress SHIFT + UP/DOWN to move.\nHold ALT to type in RAINBOW!!!!\n\n", 1, glm::vec4(0.5,0,0,1), defaultFace);

		Label->AppendText(U"t", 1, glm::vec4(1,0,0,1), defaultFace);
		Label->AppendText(U"y", 1, glm::vec4(0,1,0,1), defaultFace);
		Label->AppendText(U"p", 1, glm::vec4(0,0,1,1), defaultFace);
		Label->AppendText(U"e", 1, glm::vec4(0,1,1,1), defaultFace);
		Label->AppendText(U"!", 1, glm::vec4(1,1,0,1), defaultFace);
		Label->SetCaretPosition(Label->GetText().size());
		break;
	
	case 1:
		setTextFile("q1.txt");
		break;
	case 2:
		setTextFile("q2.txt");
		break;
	case 3:
		setTextFile("q3.txt");
		break;
	case 4:
		setTextFile("q4.txt");
		break;
	case 5:
		scale = 0.7;
		verticalTransform = 5;
		setTextFile("label.cpp");
		Label->SetCaretPosition(0);
		break;
	}
	
	i++;
}

int main()
{
	// Create a window
	if(!glfwInit())
	{
		fprintf(stderr, "Failed to initialize GLFW.\n");
		return -1;
	}

	glfwWindowHint(GLFW_SAMPLES, 4);
	glfwWindowHint(GLFW_DEPTH_BITS, 0);
	glfwWindowHint(GLFW_STENCIL_BITS, 0);
	// glfwWindowHint(GLFW_ALPHA_BITS, 8);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	
	GLFWwindow *window = glfwCreateWindow(kWidth, kHeight, "Vector-Based GPU Text Rendering", NULL, NULL);
	if(!window)
	{
		fprintf(stderr, "Failed to create GLFW window.");
		glfwTerminate();
		return -1;
	}
	
	glfwSetKeyCallback(window, onKeyPress);
	glfwSetCharModsCallback(window, onCharTyped);
	glfwSetScrollCallback(window, onScroll);
	
	// Create OpenGL context
	glfwMakeContextCurrent(window);
	glewExperimental = true;
	if(glewInit() != GLEW_OK)
	{
		fprintf(stderr, "Failed to initialize GLEW.\n");
		glfwDestroyWindow(window);
		glfwTerminate();
		return -1;
	}
	
	printf("GL Version: %s\n", glGetString(GL_VERSION));

	GLuint vertexArrayId;
	glGenVertexArrays(1, &vertexArrayId);
	glBindVertexArray(vertexArrayId);
	
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);
	
	Label = new GLLabel();
	Label->ShowCaret(true);
	defaultFace = GLFontManager::GetFontManager()->GetDefaultFont();
	boldFace = GLFontManager::GetFontManager()->GetFontFromPath("/usr/share/fonts/TTF/DroidSans-Bold.ttf");
	loadNextText();
	
	glm::mat4 kT0 = glm::translate(glm::mat4(), glm::vec3(0, 0, 0));
	
	GLLabel fpsLabel;
	glm::mat4 fpsTransform = glm::scale(glm::translate(glm::mat4(), glm::vec3(-1, 0.9, 0)), glm::vec3(kHeight/30000000.0, kWidth/30000000.0, 1.0));
	
	GLuint fb=0;
	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	// GLenum fboBuffers[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	// glDrawBuffers(2, fboBuffers);
	// glFramebufferTexture2D(CP_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mainBuffer, 0);
	
	GLuint fbTex;
	glGenTextures(1, &fbTex);
	glBindTexture(GL_TEXTURE_2D, fbTex);
	// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, kWidth, kHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	
	// The depth buffer
	GLuint depthrenderbuffer;
	glGenRenderbuffers(1, &depthrenderbuffer);
	glBindRenderbuffer(GL_RENDERBUFFER, depthrenderbuffer);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, kWidth, kHeight);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthrenderbuffer);
	
	// Set "renderedTexture" as our colour attachement #0
	glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, fbTex, 0);

	// Set the list of draw buffers.
	GLenum DrawBuffers[1] = {GL_COLOR_ATTACHMENT0};
	glDrawBuffers(1, DrawBuffers); // "1" is the size of DrawBuffers
	
	// Always check that our framebuffer is ok
	if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		printf("ERRORRRR\n");
	
	// float fullscreenQuad[] = // x, y, z, u, v
	// {
	// 	-0.5, -0.5, 0, 0, 0,
	// 	0.5, -0.5, 0, 1, 0,
	// 	-0.5, 0.5, 0, 0, 1,
	// 	
	// 	0.5, -0.5, 0, 1, 0,
	// 	0.5, 0.5, 0, 1, 1,
	// 	-0.5, 0.5, 0, 0, 1
	// };
	
	float size = 1;
	
	std::vector<CPoint> points;
	CPoint a = {glm::vec3(size, size, 0), glm::vec3(1,1,0), glm::vec2(1, 1)};
	CPoint b = {glm::vec3(size, -size, 0), glm::vec3(1,0,1), glm::vec2(1, 0)};
	CPoint c = {glm::vec3(-size, -size, 0), glm::vec3(1,1,1), glm::vec2(0, 0)};
	CPoint d = {glm::vec3(-size, size, 0), glm::vec3(0,1,1), glm::vec2(0, 1)};
	points.push_back(a);	
	points.push_back(b);
	points.push_back(c);
	points.push_back(a);
	points.push_back(c);
	points.push_back(d);
	
	// // 
	// static const float fullscreenQuad[] = {
	//    -1.0f, -1.0f, 0.0f,
	//    1.0f, -1.0f, 0.0f,
	//    0.0f,  1.0f, 0.0f,
	// };
	// 
	GLuint fsQuadBuffer;
	glGenBuffers(1, &fsQuadBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, fsQuadBuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(CPoint)*points.size(), &points[0], GL_STATIC_DRAW);
	// printf("size: %i\n", sizeof(fullscreenQuad));
	
	GLuint fuzzShader = loadShaderProgramFile("vert.glsl", "frag.glsl");
	GLuint warpShader = loadShaderProgramFile("vert.glsl", "frag2.glsl");
	glUseProgram(warpShader);
	// printf("warp: %i\n", warpShader);
	
	GLuint uWarpTime = glGetUniformLocation(warpShader, "t");
	GLuint uWarpSampler = glGetUniformLocation(warpShader, "uSampler");
	glUniform1i(uWarpSampler, 0);
	
	glUseProgram(fuzzShader);

	GLuint uFuzzTime = glGetUniformLocation(fuzzShader, "t");
	GLuint uFuzzSampler = glGetUniformLocation(fuzzShader, "uSampler");
	glUniform1i(uFuzzSampler, 0);

	
	int fpsFrame = 0;
	double fpsStartTime = glfwGetTime();
	while(!glfwWindowShouldClose(window))
	{
		float time = glfwGetTime();
		
		// glActiveTexture(GL_TEXTURE0);
		glBindFramebuffer(GL_FRAMEBUFFER, regular ? 0 : fb);
		glClearColor(160/255.0, 169/255.0, 175/255.0, 1.0);
		glClear(GL_COLOR_BUFFER_BIT);
		//
		glm::mat4 S0 = glm::scale(glm::mat4(), glm::vec3(kHeight/50000000.0, kWidth/50000000.0, 1.0));
		glm::mat4 T = glm::translate(glm::mat4(), glm::vec3(-0.9 + horizontalTransform, verticalTransform, 0));
		glm::mat4 R = glm::rotate(glm::mat4(), time/3, glm::vec3(0.0,0.0,1.0));
		glm::mat4 S = glm::scale(glm::mat4(), glm::vec3(sin(time)/6000.0, cos(time)/12000.0, 1.0));
		glm::mat4 S1 = glm::scale(glm::mat4(), glm::vec3(scale, scale, 1.0));
		Label->Render(time, spin ? (kT0*R*S) : (S1*T*S0));
		// 
		// // glViewport(0,0,kWidth,kHeight);
		// // glClearColor(1, 0, 0, 0.1);
		// // glClear(GL_COLOR_BUFFER_BIT);
		// // glBindBuffer(GL_ARRAY_BUFFER, fsQuadBuffer);
		// // glEnableVertexAttribArray(0);
		// // glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CPoint), (void*)0);
		// // glVertexAttribPointer(1, 3, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(float), (void*)(sizeof(float)*3));
		// // glVertexAttribPointer(2, 2, GL_FLOAT, GL_TRUE, sizeof(float), (void*)(sizeof(float)*6));
		// // glUseProgram(colorShader);
		// // glDrawArrays(GL_TRIANGLES, 0, 6);
		// // glDisableVertexAttribArray(0);
		// 
		if(!regular)
		{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glClearColor(160/255.0, 169/255.0, 175/255.0, 1.0);
		glClear(GL_COLOR_BUFFER_BIT);
	
		glViewport(0,0,kWidth,kHeight);
		// 
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, fbTex);
		glBindBuffer(GL_ARRAY_BUFFER, fsQuadBuffer);
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CPoint), (void*)0);
		glVertexAttribPointer(1, 3, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(CPoint), (void*)(sizeof(float)*3));
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(CPoint), (void*)(sizeof(float)*6));
		if(fuzz)
		{
			glUseProgram(fuzzShader);
			glUniform2f(uFuzzTime, time*100, time*100);
		}
		else
		{
			glUseProgram(warpShader);
			glUniform2f(uWarpTime, time, time);
		}
		glDrawArrays(GL_TRIANGLES, 0, 6);
		glDisableVertexAttribArray(0);
		glDisableVertexAttribArray(1);
		glDisableVertexAttribArray(2);
	}
		
		fpsLabel.Render(time, fpsTransform);
		
		glfwPollEvents();
		glfwSwapBuffers(window);
		
		// FPS Counter
		fpsFrame ++;
		if(fpsFrame >= 20)
		{
			double endTime = glfwGetTime();
			double fps = fpsFrame / (endTime - fpsStartTime);
			fpsFrame = 0;
			fpsStartTime = endTime;
			
			std::ostringstream stream;
			stream << "FPS: ";
			stream << std::fixed << std::setprecision(1) << fps;
			fpsLabel.SetText(toUTF32(stream.str()), 1, glm::vec4(0,0,0,1), defaultFace);
		}
	}
	
	// Exit
	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}

static bool leftShift = false;
static bool rightShift = false;

void onKeyPress(GLFWwindow *window, int key, int scanCode, int action, int mods)
{
	if(action == GLFW_PRESS && key == GLFW_KEY_LEFT_SHIFT)
		leftShift = true;
	else if(action == GLFW_RELEASE && key == GLFW_KEY_LEFT_SHIFT)
		leftShift = false;
	else if(action == GLFW_PRESS && key == GLFW_KEY_RIGHT_SHIFT)
		rightShift = true;
	else if(action == GLFW_RELEASE && key == GLFW_KEY_RIGHT_SHIFT)
		rightShift = false;

	if(action == GLFW_RELEASE)
		return;
		
	if(key == GLFW_KEY_BACKSPACE)
	{
		std::u32string text = Label->GetText();
		if(text.size() > 0 && Label->GetCaretPosition() > 0)
		{
			Label->RemoveText(Label->GetCaretPosition()-1, 1);
			Label->SetCaretPosition(Label->GetCaretPosition() - 1);
		}
	}
	else if(key == GLFW_KEY_ENTER)
	{
		Label->InsertText(U"\n", Label->GetCaretPosition(), 1, glm::vec4(0,0,0,1), rightShift?boldFace:defaultFace);
		Label->SetCaretPosition(Label->GetCaretPosition() + 1);
	}
	else if(key == GLFW_KEY_ESCAPE)
	{
		spin = !spin;
	}
	else if(key == GLFW_KEY_LEFT && (mods & GLFW_MOD_SHIFT) != GLFW_MOD_SHIFT)
	{
		Label->SetCaretPosition(Label->GetCaretPosition() - 1);
	}
	else if(key == GLFW_KEY_RIGHT && (mods & GLFW_MOD_SHIFT) != GLFW_MOD_SHIFT)
	{
		Label->SetCaretPosition(Label->GetCaretPosition() + 1);
	}
	else if(key == GLFW_KEY_DOWN && (mods & GLFW_MOD_SHIFT) != GLFW_MOD_SHIFT)
	{
		fuzz = !fuzz;
	}
	else if(key == GLFW_KEY_UP && (mods & GLFW_MOD_SHIFT) != GLFW_MOD_SHIFT)
	{
		regular = !regular;
	}
	else if(key == GLFW_KEY_DOWN)
	{
		verticalTransform += 0.2 / scale;
	}
	else if(key == GLFW_KEY_UP)
	{
		verticalTransform -= 0.2 / scale;
	}
	else if(key == GLFW_KEY_LEFT)
	{
		horizontalTransform += 0.2 / scale;
	}
	else if(key == GLFW_KEY_RIGHT)
	{
		horizontalTransform -= 0.2 / scale;
	}
	//else if(key == GLFW_KEY_F3)
	//{
	//	defaultFace = GLFontManager::GetFontManager()->GetFontFromPath("/usr/share/fonts/TTF/DroidSans-Bold.ttf");
	//}
	//else if(key == GLFW_KEY_F4)
	//{
	//	defaultFace = GLFontManager::GetFontManager()->GetDefaultFont();
	//}
	else if(key == GLFW_KEY_TAB)
	{
		loadNextText();
	}
}

void onCharTyped(GLFWwindow *window, unsigned int codePoint, int mods)
{
	double r0 = 0, r1 = 0, r2 = 0;

	if((mods & GLFW_MOD_ALT) == GLFW_MOD_ALT)
	{
		r0 = ((double) rand() / (RAND_MAX-1));
		r1 = ((double) rand() / (RAND_MAX-1));
		r2 = ((double) rand() / (RAND_MAX-1));
	}

	Label->InsertText(std::u32string(1, codePoint), Label->GetCaretPosition(), 1, glm::vec4(r0,r1,r2,1), rightShift?boldFace:defaultFace);
	Label->SetCaretPosition(Label->GetCaretPosition() + 1);
}

void onScroll(GLFWwindow *window, double deltaX, double deltaY)
{
	if(leftShift)
	{
		scale += 0.1*deltaY;
		if(scale < 0.1)
			scale = 0.1;
	}
	else
	{
		horizontalTransform += 0.1*deltaX/scale;
		verticalTransform -= 0.1*deltaY/scale;
	}
}

std::u32string toUTF32(const std::string &s)
{
	std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> conv;
	return conv.from_bytes(s);
}

static GLuint loadShaderProgramFile(const char *vertexShaderPath, const char *fragmentShaderPath)
{
	// Compile vertex shader
	std::ifstream vsStream(vertexShaderPath);
	std::string vsCode((std::istreambuf_iterator<char>(vsStream)), (std::istreambuf_iterator<char>()));
	const char *vsCodeC = vsCode.c_str();
	
	GLuint vertexShaderId = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShaderId, 1, &vsCodeC, NULL);
	glCompileShader(vertexShaderId);
	
	GLint result = GL_FALSE;
	int infoLogLength = 0;
	glGetShaderiv(vertexShaderId, GL_COMPILE_STATUS, &result);
	glGetShaderiv(vertexShaderId, GL_INFO_LOG_LENGTH, &infoLogLength);
	if(infoLogLength > 1)
	{
		std::vector<char> infoLog(infoLogLength+1);
		glGetShaderInfoLog(vertexShaderId, infoLogLength, NULL, &infoLog[0]);
		printf("[Vertex] %s\n", &infoLog[0]);
	}
	if(!result)
		return 0;

	// Compile fragment shader
	std::ifstream fsStream(fragmentShaderPath);
	std::string fsCode((std::istreambuf_iterator<char>(fsStream)), (std::istreambuf_iterator<char>()));
	const char *fsCodeC = fsCode.c_str();
	
	GLuint fragmentShaderId = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragmentShaderId, 1, &fsCodeC, NULL);
	glCompileShader(fragmentShaderId);
	
	result = GL_FALSE, infoLogLength = 0;
	glGetShaderiv(fragmentShaderId, GL_COMPILE_STATUS, &result);
	glGetShaderiv(fragmentShaderId, GL_INFO_LOG_LENGTH, &infoLogLength);
	if(infoLogLength > 1)
	{
		std::vector<char> infoLog(infoLogLength);
		glGetShaderInfoLog(fragmentShaderId, infoLogLength, NULL, &infoLog[0]);
		printf("[Fragment] %s\n", &infoLog[0]);
	}
	if(!result)
		return 0;
	
	// Link the program
	GLuint programId = glCreateProgram();
	glAttachShader(programId, vertexShaderId);
	glAttachShader(programId, fragmentShaderId);
	glLinkProgram(programId);

	result = GL_FALSE, infoLogLength = 0;
	glGetProgramiv(programId, GL_LINK_STATUS, &result);
	glGetProgramiv(programId, GL_INFO_LOG_LENGTH, &infoLogLength);
	if(infoLogLength > 1)
	{
		std::vector<char> infoLog(infoLogLength+1);
		glGetProgramInfoLog(programId, infoLogLength, NULL, &infoLog[0]);
		printf("[Shader Linker] %s\n", &infoLog[0]);
	}
	if(!result)
		return 0;

	glDetachShader(programId, vertexShaderId);
	glDetachShader(programId, fragmentShaderId);
	
	glDeleteShader(vertexShaderId);
	glDeleteShader(fragmentShaderId);

	return programId;
}
