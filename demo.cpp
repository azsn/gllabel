/*
 * zelbrium <zelbrium@gmail.com>, 2017.
 *
 * Demo code for GLLabel. Depends on GLFW3, GLEW, GLM, FreeType2, and C++11.
 */

#include "label.hpp"
#include <glfw3.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>
#include <codecvt>
#include <iomanip>
#include <string>
#include <sstream>
#include <iostream>

static uint32_t width = 1280;
static uint32_t height = 800;

static GLLabel *Label;
static bool spin = false;
static FT_Face defaultFace;
static FT_Face boldFace;
float horizontalTransform = -0.9;
float verticalTransform = 0.6;
float scale = 1;

void onKeyPress(GLFWwindow *window, int key, int scanCode, int action, int mods);
void onCharTyped(GLFWwindow *window, unsigned int codePoint, int mods);
void onScroll(GLFWwindow *window, double deltaX, double deltaY);
void onResize(GLFWwindow *window, int width, int height);
std::u32string toUTF32(const std::string &s);
static glm::vec3 pt(float pt);

int main()
{
	// Create a window
	if (!glfwInit()) {
		fprintf(stderr, "Failed to initialize GLFW.\n");
		return -1;
	}

	glfwWindowHint(GLFW_SAMPLES, 8);
	glfwWindowHint(GLFW_DEPTH_BITS, 0);
	glfwWindowHint(GLFW_STENCIL_BITS, 0);
	glfwWindowHint(GLFW_ALPHA_BITS, 8);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	GLFWwindow *window = glfwCreateWindow(width, height, "Vector-Based GPU Text Rendering", NULL, NULL);
	if (!window) {
		fprintf(stderr, "Failed to create GLFW window.");
		glfwTerminate();
		return -1;
	}

	glfwSetKeyCallback(window, onKeyPress);
	glfwSetCharModsCallback(window, onCharTyped);
	glfwSetScrollCallback(window, onScroll);
	glfwSetWindowSizeCallback(window, onResize);

	// Create OpenGL context
	glfwMakeContextCurrent(window);
	glewExperimental = true;
	if (glewInit() != GLEW_OK) {
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

	// Create new label
	Label = new GLLabel();
	Label->ShowCaret(true);

	printf("Loading font files\n");
	defaultFace = GLFontManager::GetFontManager()->GetDefaultFont();
	boldFace = GLFontManager::GetFontManager()->GetFontFromPath("fonts/LiberationSans-Bold.ttf");

	Label->SetText(U"Welcome to vector-based GPU text rendering!\nType whatever you want!\n\nPress LEFT/RIGHT to move cursor.\nPress ESC to toggle rotate.\nScroll vertically/horizontally to move.\nScroll while holding shift to zoom.\nRight-shift for bold.\nHold ALT to type in ", 1, glm::vec4(0.5,0,0,1), defaultFace);
	Label->AppendText(U"r", 1, glm::vec4(0.58, 0, 0.83, 1), defaultFace);
	Label->AppendText(U"a", 1, glm::vec4(0.29, 0, 0.51, 1), defaultFace);
	Label->AppendText(U"i", 1, glm::vec4(0,    0, 1,    1), defaultFace);
	Label->AppendText(U"n", 1, glm::vec4(0,    1, 0,    1), defaultFace);
	Label->AppendText(U"b", 1, glm::vec4(1,    1, 0,    1), defaultFace);
	Label->AppendText(U"o", 1, glm::vec4(1,    0.5, 0,  1), defaultFace);
	Label->AppendText(U"w", 1, glm::vec4(1,    0, 0,    1), defaultFace);
	Label->AppendText(U"!\n", 1, glm::vec4(0.5,0,0,1), defaultFace);
	Label->SetCaretPosition(Label->GetText().size());

	GLLabel fpsLabel;
	fpsLabel.SetText(toUTF32("FPS:"), 1, glm::vec4(0,0,0,1), defaultFace);

	printf("Starting render\n");

	int fpsFrame = 0;
	double fpsStartTime = glfwGetTime();
	while (!glfwWindowShouldClose(window)) {
		float time = glfwGetTime();

		glClearColor(160/255.0, 169/255.0, 175/255.0, 1.0);
		glClear(GL_COLOR_BUFFER_BIT);

		glm::vec3 userTranslation(horizontalTransform, verticalTransform, 0);
		glm::vec3 userScale(scale, scale, 1.0);

		glm::mat4 textMat(1.0);
		textMat = glm::scale(textMat, userScale);
		textMat = glm::translate(textMat, userTranslation);
		if (spin) {
			textMat = glm::rotate(textMat, time/3, glm::vec3(0.0,0.0,1.0));
			textMat = glm::scale(textMat, glm::vec3(sin(time)*2, cos(time), 1.0));
		}
		textMat = glm::scale(textMat, pt(8));
		Label->Render(time, textMat);

		// Window size might change, so recalculate this (and other pt() calls)
		glm::mat4 fpsMat(1.0);
		fpsMat = glm::scale(fpsMat, userScale);
		fpsMat = glm::translate(fpsMat, userTranslation + glm::vec3(0, 0.2, 0));
		if (spin) {
			fpsMat = glm::translate(fpsMat, glm::vec3(0.1, 0, 0));
			fpsMat = glm::rotate(fpsMat, time*4, glm::vec3(0,0,1));
			fpsMat = glm::translate(fpsMat, glm::vec3(-0.1, 0, 0));
		}
		fpsMat = glm::scale(fpsMat, pt(7));
		fpsLabel.Render(time, fpsMat);

		glfwPollEvents();
		glfwSwapBuffers(window);

		// FPS Counter
		fpsFrame ++;
		if (fpsFrame >= 30) {
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
	if (action == GLFW_PRESS && key == GLFW_KEY_LEFT_SHIFT) {
		leftShift = true;
	} else if (action == GLFW_RELEASE && key == GLFW_KEY_LEFT_SHIFT) {
		leftShift = false;
	} else if (action == GLFW_PRESS && key == GLFW_KEY_RIGHT_SHIFT) {
		rightShift = true;
	} else if (action == GLFW_RELEASE && key == GLFW_KEY_RIGHT_SHIFT) {
		rightShift = false;
	}

	if (action == GLFW_RELEASE) {
		return;
	}

	if (key == GLFW_KEY_BACKSPACE) {
		std::u32string text = Label->GetText();
		if (text.size() > 0 && Label->GetCaretPosition() > 0) {
			Label->RemoveText(Label->GetCaretPosition()-1, 1);
			Label->SetCaretPosition(Label->GetCaretPosition() - 1);
		}
	} else if (key == GLFW_KEY_ENTER) {
		Label->InsertText(U"\n", Label->GetCaretPosition(), 1, glm::vec4(0,0,0,1), rightShift?boldFace:defaultFace);
		Label->SetCaretPosition(Label->GetCaretPosition() + 1);
	} else if (key == GLFW_KEY_ESCAPE) {
		spin = !spin;
	} else if (key == GLFW_KEY_LEFT) {
		Label->SetCaretPosition(Label->GetCaretPosition() - 1);
	} else if (key == GLFW_KEY_RIGHT) {
		Label->SetCaretPosition(Label->GetCaretPosition() + 1);
	}
}

void onCharTyped(GLFWwindow *window, unsigned int codePoint, int mods)
{
	double r0 = 0, r1 = 0, r2 = 0;

	if ((mods & GLFW_MOD_ALT) == GLFW_MOD_ALT) {
		r0 = ((double) rand() / (RAND_MAX-1));
		r1 = ((double) rand() / (RAND_MAX-1));
		r2 = ((double) rand() / (RAND_MAX-1));
	}

	Label->InsertText(std::u32string(1, codePoint), Label->GetCaretPosition(), 1, glm::vec4(r0,r1,r2,1), rightShift?boldFace:defaultFace);
	Label->SetCaretPosition(Label->GetCaretPosition() + 1);
}

void onScroll(GLFWwindow *window, double deltaX, double deltaY)
{
	if (leftShift) {
		scale += 0.1*deltaY;
		if (scale < 0.1) {
			scale = 0.1;
		}
	} else {
		horizontalTransform += 0.1*deltaX/scale;
		verticalTransform -= 0.1*deltaY/scale;
	}
}

void onResize(GLFWwindow *window, int w, int h)
{
	width = w;
	height = h;
	glViewport(0,0,w,h);
}

std::u32string toUTF32(const std::string &s)
{
	std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> conv;
	return conv.from_bytes(s);
}

// Converts font points into a glm::vec3 scalar.
static glm::vec3 pt(float pt)
{
	static const float emUnits = 1.0/2048.0;
	const float aspect = (float)height / (float)width;

	float scale = emUnits * pt / 72.0;
	return glm::vec3(scale * aspect, scale, 0);
}
