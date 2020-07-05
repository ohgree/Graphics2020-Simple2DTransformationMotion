#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <GL/glew.h>
#include <GL/freeglut.h>
#include <vector>
#include <time.h>

#include "Shaders/LoadShaders.h"
GLuint h_ShaderProgram; // handle to shader program
GLint loc_ModelViewProjectionMatrix, loc_primitive_color; // indices of uniform variables

// include glm/*.hpp only if necessary
//#include <glm/glm.hpp> 
#include <glm/gtc/matrix_transform.hpp> //translate, rotate, scale, ortho, etc.
glm::mat4 ModelViewProjectionMatrix;
glm::mat4 ViewMatrix, ProjectionMatrix, ViewProjectionMatrix;

#define TO_RADIAN 0.01745329252f  
#define TO_DEGREE 57.295779513f
#define BUFFER_OFFSET(offset) ((GLvoid *) (offset))

#define LOC_VERTEX 0

int win_width = 0, win_height = 0;
float centerx = 0.0f, centery = 0.0f;

/**
 * custom variables
 */
typedef struct _position {
	float x = 0.0f;
	float y = 0.0f;
} position2d;
position2d destination;
position2d player;
float player_rotation = 0.0f;
float player_velocity = 0.0f;
#define DECELERATION -0.3f
#define ACELLERATION 0.5f
#define FLARE_DECELERATION -0.4f
#define FLARE_INITIAL_SPEED 10.0f
#define BULLET_SPEED 20.0f
#define MISSLE_INITIAL_SPEED 0.0f
#define MISSLE_ACCELERATION 0.3f
#define PROJECTILE_TIMEOUT_DEFAULT 3000 // approx. 30 seconds
#define AFTERBURNER_SPEED_LIMIT 20.0f
#define SPEED_THRESHOLD 0.01f
#define HITBOX_DISTANCE 15.0f
#define TARGET_SPAWN_TICKS 150 //approx. 1.5 seconds

class Projectile {
public:
	position2d position;
	glm::vec3 vector;
	glm::vec3 acceleration;
	float orientation;
	int timeout;
	bool bounded;
public:
	Projectile(position2d position, glm::vec3 vector, glm::vec3 acceleration, int timeout, bool bounded) {
		this->position = position;
		this->vector = vector;
		this->acceleration = acceleration;
		this->orientation = getDirectionalRotation();
		this->timeout = timeout;
		this->bounded = bounded;
	}
	Projectile(position2d position, glm::vec3 vector, glm::vec3 acceleration, int timeout) : Projectile(position, vector, acceleration, timeout, true) {}
	Projectile(float x, float y, float rotation, float speed, float acceleration, int timeout) :
		Projectile({ x, y }, glm::vec3(speed* cosf(rotation), speed* sinf(rotation), 0.0f), glm::vec3(cosf(rotation)* acceleration, sinf(rotation)* acceleration, 0.0f), timeout) {}
	Projectile(float x, float y, float rotation, float speed, float acceleration) : Projectile(x, y, rotation, speed, acceleration, PROJECTILE_TIMEOUT_DEFAULT) {};
	Projectile(float x, float y, float rotation, float speed) : Projectile(x, y, rotation, speed, 0.0f) {};
	float getSpeed() {
		return sqrt(pow(vector.x, 2) + pow(vector.y, 2)) ;
	}
	float getDirectionalRotation() {
		float rotation = atanf(vector.y / vector.x);
		if (vector.x < 0)
			rotation += 180 * TO_RADIAN;
		return rotation;
	}
	
	void move() {
		position.x += vector.x;
		position.y += vector.y;

		vector += acceleration;

		timeout--;
	}
	float getOrientation() {
		return this->orientation;
	}
	bool isValid() {
		
		if (timeout <= 0)
			return false;

		if (bounded)
			if (win_width / 2.0f < abs(position.x) || win_height / 2.0f < abs(position.y))
				return false;

		return true;
	}
	position2d getPosition() {
		return position;
	}
};

class Controllable {
private:
	glm::vec3 position, vector, destination;
public:
	Controllable(glm::vec3 position, glm::vec3 vector) {
		this->position = position;
		this->vector = vector;
	}
	void setDest(glm::vec3 destination) {
		this->destination = destination;
	}
	void move() {
		vector = (destination - position) / 15.0f;
		if(getSpeed() > SPEED_THRESHOLD)
			position += vector;
	}
	float getDirectionalRotation() {
		float rotation = atanf(vector.y / vector.x);
		if (vector.x < 0)
			rotation += 180 * TO_RADIAN;
		else if (vector.x == 0) {
			rotation = vector.y > 0 ? -90.0f * TO_RADIAN : 90.0f * TO_RADIAN;
		}
		return rotation;
	}
	glm::vec3 getPosition() { return position; }
	float getSpeed() {
		return sqrt(pow(vector.x, 2) + pow(vector.y, 2));
	}
	glm::vec3 getDestination() {
		return destination;
	}
};

std::vector<Projectile> projectiles;
std::vector<Projectile> rockets;
Controllable plane(glm::vec3(0.0f), glm::vec3(0.0f));

std::vector<Projectile> targets;
std::vector<Projectile> fireworks;
std::vector<Projectile> smokes;


glm::vec3 rot_to_vec3(float rotation) {
	return glm::vec3(cosf(rotation), sinf(rotation), 0.0f);
}

// 2D 물체 정의 부분은 objects.h 파일로 분리
// 새로운 물체 추가 시 prepare_scene() 함수에서 해당 물체에 대한 prepare_***() 함수를 수행함.
// (필수는 아니나 올바른 코딩을 위하여) cleanup() 함수에서 해당 resource를 free 시킴.
#include "objects.h"

bool rmb = false;
unsigned int timestamp = 0;
void timer(int value) {
	timestamp = (timestamp + 1) % UINT_MAX;

	if (timestamp % TARGET_SPAWN_TICKS == 0) {
		srand(time(NULL));
		targets.push_back(*new Projectile((float)(rand() % win_width) - win_width / 2.0f, (float)(rand() % win_height) - win_height / 2.0f, rand() % 360 * TO_RADIAN, 10.0f / (rand() % 4 + 2)));
	}

	/* move plane */
	plane.move();
	//printf("%f\n", plane.getDirectionalRotation());

	/* fire gun every 10 ticks */
	static int gun_interval = 10;
	if (rmb && --gun_interval <= 0) {
		projectiles.push_back(*new Projectile(plane.getPosition().x, plane.getPosition().y, plane.getDirectionalRotation(), BULLET_SPEED + plane.getSpeed()));
		gun_interval = 10;
	}

	/* move & erase rockets */
	for (int i = 0; i < rockets.size(); i++) {
		if (!rockets[i].isValid()) {
			rockets.erase(rockets.begin() + i--);
			continue;
		}
		position2d pos = rockets[i].getPosition();
		rockets[i].move();
		if (rand() % 5 == 0)
			smokes.push_back(*new Projectile(pos.x, pos.y, rockets[i].getDirectionalRotation() + 180 * TO_RADIAN, 3.0f, -0.1f, 30));

		for (int j = 0; j < targets.size(); j++) {
			float posX = rockets[i].getPosition().x;
			float posY = rockets[i].getPosition().y;
			if (HITBOX_DISTANCE > sqrt(pow(posX - targets[j].getPosition().x, 2) + pow(posY - targets[j].getPosition().y, 2))) {
				targets.erase(targets.begin() + j--);
				for (int i = 0; i < 360; i += rand() % 50) {
					Projectile p({ posX, posY },
						glm::vec3(FLARE_INITIAL_SPEED * rot_to_vec3(i * TO_RADIAN)), FLARE_DECELERATION * rot_to_vec3(i * TO_RADIAN), 600, false);
					fireworks.push_back(p);
				}
			}
		}
	}

	/* move & erase smokes */
	for (int i = 0; i < smokes.size(); i++) {
		if (!smokes[i].isValid()) {
			smokes.erase(smokes.begin() + i--);
			continue;
		}
		smokes[i].move();
	}

	/* move & erase projectiles */
	for (int i = 0; i < projectiles.size(); i++) {
		if (!projectiles[i].isValid()) {
			projectiles.erase(projectiles.begin() + i--);
			continue;
		}
		projectiles[i].move();
		for (int j = 0; j < targets.size(); j++) {
			float posX = projectiles[i].getPosition().x;
			float posY = projectiles[i].getPosition().y;
			if (HITBOX_DISTANCE > sqrt(pow(posX - targets[j].getPosition().x, 2) + pow(posY - targets[j].getPosition().y, 2))) {
				targets.erase(targets.begin() + j--);
				for (int i = 0; i < 360; i += rand() % 50) {
					Projectile p({ posX, posY },
						glm::vec3(FLARE_INITIAL_SPEED * rot_to_vec3(i * TO_RADIAN)), FLARE_DECELERATION * rot_to_vec3(i * TO_RADIAN), 600, false);
					fireworks.push_back(p);
				}
			}
		}
	}

	/* move & erase fireworks */
	for (int i = 0; i < fireworks.size(); i++) {
		if (!fireworks[i].isValid()) {
			fireworks.erase(fireworks.begin() + i--);
			continue;
		}
		fireworks[i].move();
	}

	/* move targets */
	for (int i = 0; i < targets.size(); i++) {
		if (!targets[i].isValid()) {
			if (win_width / 2.0f < abs(targets[i].position.x))
				targets[i].vector.x = -targets[i].vector.x;

			if (win_height / 2.0f < abs(targets[i].position.y))
				targets[i].vector.y = -targets[i].vector.y;
		}

		targets[i].orientation += 5*TO_RADIAN;
		targets[i].move();
	}

	glutPostRedisplay();
	glutTimerFunc(10, timer, 0);
}


void display(void) {
	glm::mat4 ModelMatrix;

	glClear(GL_COLOR_BUFFER_BIT);
	// static object: axes, house
	ModelMatrix = glm::mat4(1.0f);
	ModelViewProjectionMatrix = ViewProjectionMatrix * ModelMatrix;
	glUniformMatrix4fv(loc_ModelViewProjectionMatrix, 1, GL_FALSE, &ModelViewProjectionMatrix[0][0]);
	draw_axes();

	float target_degree = (float)(((int)timestamp % 722) - 360);
	//printf("%f\n", fabs(target_degree));
	for (Projectile p : targets) {
		ModelMatrix = glm::mat4(1.0f);
		ModelMatrix = glm::translate(ModelMatrix, glm::vec3(p.getPosition().x, p.getPosition().y, 0.0f));
		ModelMatrix = glm::rotate(ModelMatrix, p.getOrientation(), glm::vec3(0.0f, 0.0f, 1.0f));
		ModelMatrix = glm::scale(ModelMatrix, glm::vec3((fabs(target_degree)+360.0f)/360.0f));

		ModelViewProjectionMatrix = ViewProjectionMatrix * ModelMatrix;
		glUniformMatrix4fv(loc_ModelViewProjectionMatrix, 1, GL_FALSE, &ModelViewProjectionMatrix[0][0]);
		draw_car2();
	}


	if (plane.getSpeed() > SPEED_THRESHOLD) {
		ModelMatrix = glm::mat4(1.0f);
		ModelMatrix = glm::translate(ModelMatrix, plane.getDestination());
		ModelMatrix = glm::scale(ModelMatrix, glm::vec3(3.0f));
		ModelViewProjectionMatrix = ViewProjectionMatrix * ModelMatrix;
		glUniformMatrix4fv(loc_ModelViewProjectionMatrix, 1, GL_FALSE, &ModelViewProjectionMatrix[0][0]);
		draw_crosshair();
	}


	ModelMatrix = glm::mat4(1.0f);
	ModelMatrix = glm::translate(ModelMatrix, plane.getPosition());
	ModelMatrix = glm::rotate(ModelMatrix, plane.getDirectionalRotation() + 90.0f * TO_RADIAN, glm::vec3(0.0f, 0.0f, 1.0f));
	ModelViewProjectionMatrix = ViewProjectionMatrix * ModelMatrix;
	glUniformMatrix4fv(loc_ModelViewProjectionMatrix, 1, GL_FALSE, &ModelViewProjectionMatrix[0][0]);
	draw_airplane();

	/* projectiles - sword */
	for (Projectile p : projectiles) {
		ModelMatrix = glm::mat4(1.0f);
		ModelMatrix = glm::translate(ModelMatrix, glm::vec3(p.getPosition().x, p.getPosition().y, 0.0f));
		float projectile_rotation = -90 * TO_RADIAN + p.getDirectionalRotation();
		ModelMatrix = glm::rotate(ModelMatrix, projectile_rotation, glm::vec3(0.0f, 0.0f, 1.0f));

		ModelViewProjectionMatrix = ViewProjectionMatrix * ModelMatrix;
		glUniformMatrix4fv(loc_ModelViewProjectionMatrix, 1, GL_FALSE, &ModelViewProjectionMatrix[0][0]);
		draw_sword();
	}


	/* projectile - rockets */
	for (Projectile p: rockets) {
		ModelMatrix = glm::mat4(1.0f);
		ModelMatrix = glm::translate(ModelMatrix, glm::vec3(p.getPosition().x, p.getPosition().y, 0.0f));
		float projectile_rotation = -90 * TO_RADIAN + p.getDirectionalRotation();
		ModelMatrix = glm::rotate(ModelMatrix, projectile_rotation, glm::vec3(0.0f, 0.0f, 1.0f));
		ModelMatrix = glm::scale(ModelMatrix, glm::vec3(3.0f));

		ModelViewProjectionMatrix = ViewProjectionMatrix * ModelMatrix;
		glUniformMatrix4fv(loc_ModelViewProjectionMatrix, 1, GL_FALSE, &ModelViewProjectionMatrix[0][0]);
		draw_rocket();
	}

	/* effect - smokes */
	for (Projectile p : smokes) {
		ModelMatrix = glm::mat4(1.0f);
		ModelMatrix = glm::translate(ModelMatrix, glm::vec3(p.getPosition().x, p.getPosition().y, 0.0f));
		float projectile_rotation = -90 * TO_RADIAN + p.getDirectionalRotation();
		ModelMatrix = glm::rotate(ModelMatrix, projectile_rotation, glm::vec3(0.0f, 0.0f, 1.0f));
		ModelMatrix = glm::scale(ModelMatrix, glm::vec3(1.0f));

		ModelViewProjectionMatrix = ViewProjectionMatrix * ModelMatrix;
		glUniformMatrix4fv(loc_ModelViewProjectionMatrix, 1, GL_FALSE, &ModelViewProjectionMatrix[0][0]);
		draw_shirt();
	}

	/* projectile - fireworks */
	for (Projectile p : fireworks) {
		ModelMatrix = glm::mat4(1.0f);
		ModelMatrix = glm::translate(ModelMatrix, glm::vec3(p.getPosition().x, p.getPosition().y, 0.0f));
		float projectile_rotation = -90 * TO_RADIAN + p.getDirectionalRotation();
		ModelMatrix = glm::rotate(ModelMatrix, projectile_rotation, glm::vec3(0.0f, 0.0f, 1.0f));
		//ModelMatrix = glm::scale(ModelMatrix, glm::vec3(3.0f));

		ModelViewProjectionMatrix = ViewProjectionMatrix * ModelMatrix;
		glUniformMatrix4fv(loc_ModelViewProjectionMatrix, 1, GL_FALSE, &ModelViewProjectionMatrix[0][0]);
		draw_house();
	}

	/* afterburner flame */
	ModelMatrix = glm::mat4(1.0f);
	float scaleY = 0.4f * plane.getSpeed();
	if (plane.getSpeed() > AFTERBURNER_SPEED_LIMIT)
		scaleY = 0.4f * AFTERBURNER_SPEED_LIMIT;
	
	ModelMatrix = glm::translate(ModelMatrix, glm::vec3(plane.getPosition().x, plane.getPosition().y, 0.0f));
	ModelMatrix = glm::rotate(ModelMatrix, plane.getDirectionalRotation() - 90.0f * TO_RADIAN, glm::vec3(0.0f, 0.0f, 1.0f));
	ModelMatrix = glm::translate(ModelMatrix, glm::vec3(0.0f, -24.0f, 0.0f));
	ModelMatrix = glm::scale(ModelMatrix, glm::vec3(3.0f, scaleY, 1.0f));
	ModelViewProjectionMatrix = ViewProjectionMatrix * ModelMatrix;

	glUniformMatrix4fv(loc_ModelViewProjectionMatrix, 1, GL_FALSE, &ModelViewProjectionMatrix[0][0]);
	draw_flame();

	glFlush();
}


void keyboard(unsigned char key, int x, int y) {
	switch (key) {
	case 27: // ESC key
		glutLeaveMainLoop(); // Incur destuction callback for cleanups.
		break;
	case ' ': //missile
		rockets.push_back(*new Projectile(plane.getPosition().x, plane.getPosition().y, plane.getDirectionalRotation(), MISSLE_INITIAL_SPEED, MISSLE_ACCELERATION));
		break;
	case 'f': //fireworks
		for (int i = 0; i < 360; i += rand() % 50) {
			Projectile p({plane.getPosition().x, plane.getPosition().y}, 
				glm::vec3(FLARE_INITIAL_SPEED * rot_to_vec3(i * TO_RADIAN)), FLARE_DECELERATION * rot_to_vec3(i * TO_RADIAN), 600, false);
			fireworks.push_back(p);
		}
		break;
	case 't': //add target
		targets.push_back(*new Projectile((float)(rand() % win_width) - win_width / 2.0f, (float)(rand() % win_height) - win_height / 2.0f, rand() % 360 * TO_RADIAN, 10.0f / (rand() % 4 + 2)));
		break;
	}
}

int leftbuttonpressed = 0;
void mouse(int button, int state, int x, int y) {
	if ((button == GLUT_LEFT_BUTTON) && (state == GLUT_DOWN)) {

		destination.x = x - win_width / 2.0f;
		destination.y = (win_height - y) - win_height / 2.0f;
		
		glm::vec3 dest;
		dest.x = x - win_width / 2.0f;
		dest.y = (win_height - y) - win_height / 2.0f;
		dest.z = 0.0f;

		plane.setDest(dest);


		//printf("playerpos: %f %f, dest: %f %f\n", player.x, player.y, destination.x, destination.y);


		float dx = destination.x - player.x;
		float dy = destination.y - player.y;
		player_velocity = sqrt(pow(dx, 2) + pow(dy, 2));
		if (dx > 0.0f) {
			player_rotation = atanf(dy / dx);
		}
		else if (dx < 0.0f) {
			player_rotation = atanf(dy / dx) - 180.0f * TO_RADIAN;
		}
		else {
			player_rotation = (dy > 0.0f) ? 90.0f * TO_RADIAN : -90.0f * TO_RADIAN;
		}
		//printf("rotation: %f\nvelocity: %f", (double)player_rotation * TO_DEGREE, player_velocity);

		player.x = destination.x;
		player.y = destination.y;

		leftbuttonpressed = 1;
	}
	else if ((button == GLUT_LEFT_BUTTON) && (state == GLUT_UP))
		leftbuttonpressed = 0;
	else if ((button == GLUT_RIGHT_BUTTON) && (state == GLUT_DOWN)) {
		rmb = true;
		projectiles.push_back(*new Projectile(plane.getPosition().x, plane.getPosition().y, plane.getDirectionalRotation(), BULLET_SPEED));
	}
	else if ((button == GLUT_RIGHT_BUTTON) && (state == GLUT_UP)) {
		rmb = false;
	}
}

void motion(int x, int y) {
	if (leftbuttonpressed) {
		centerx = x - win_width / 2.0f, centery = (win_height - y) - win_height / 2.0f;


		destination.x = x - win_width / 2.0f;
		destination.y = (win_height - y) - win_height / 2.0f;


		//printf("playerpos: %f %f, dest: %f %f\n", player.x, player.y, destination.x, destination.y);

		static int delay = 0;

		if (++delay == 6) {

			glm::vec3 dest;
			dest.x = x - win_width / 2.0f;
			dest.y = (win_height - y) - win_height / 2.0f;
			dest.z = 0.0f;

			plane.setDest(dest);

			float dx = destination.x - player.x;
			float dy = destination.y - player.y;
			player_velocity = sqrt(pow(dx, 2) + pow(dy, 2));
			if (dx > 0.0f) {
				player_rotation = atanf(dy / dx);
			}
			else if (dx < 0.0f) {
				player_rotation = atanf(dy / dx) - 180.0f * TO_RADIAN;
			}
			else {
				player_rotation = (dy > 0.0f) ? 90.0f * TO_RADIAN : -90.0f * TO_RADIAN;
			}

			player.x = destination.x;
			player.y = destination.y;
			delay = 0;
		}

		glutPostRedisplay();
	}
}

void reshape(int width, int height) {
	win_width = width, win_height = height;

	glViewport(0, 0, win_width, win_height);
	ProjectionMatrix = glm::ortho(-win_width / 2.0, win_width / 2.0,
		-win_height / 2.0, win_height / 2.0, -1000.0, 1000.0);
	ViewProjectionMatrix = ProjectionMatrix * ViewMatrix;

	update_axes();

	glutPostRedisplay();
}

void cleanup(void) {
	glDeleteVertexArrays(1, &VAO_axes);
	glDeleteBuffers(1, &VBO_axes);

	glDeleteVertexArrays(1, &VAO_airplane);
	glDeleteBuffers(1, &VBO_airplane);

	glDeleteVertexArrays(1, &VAO_house);
	glDeleteBuffers(1, &VBO_house);

	glDeleteVertexArrays(1, &VAO_flame);
	glDeleteBuffers(1, &VBO_flame);

	glDeleteVertexArrays(1, &VAO_sword);
	glDeleteBuffers(1, &VBO_sword);

	glDeleteVertexArrays(1, &VAO_crosshair);
	glDeleteBuffers(1, &VBO_crosshair);

	glDeleteVertexArrays(1, &VAO_rocket);
	glDeleteBuffers(1, &VBO_rocket);

	glDeleteVertexArrays(1, &VAO_shirt);
	glDeleteBuffers(1, &VBO_shirt);
}

void register_callbacks(void) {
	glutDisplayFunc(display);
	glutKeyboardFunc(keyboard);
	glutMouseFunc(mouse);
	glutMotionFunc(motion);
	glutReshapeFunc(reshape);
	glutTimerFunc(10, timer, 0);
	glutCloseFunc(cleanup);
}

void prepare_shader_program(void) {
	ShaderInfo shader_info[3] = {
		{ GL_VERTEX_SHADER, "Shaders/simple.vert" },
		{ GL_FRAGMENT_SHADER, "Shaders/simple.frag" },
		{ GL_NONE, NULL }
	};

	h_ShaderProgram = LoadShaders(shader_info);
	glUseProgram(h_ShaderProgram);

	loc_ModelViewProjectionMatrix = glGetUniformLocation(h_ShaderProgram, "u_ModelViewProjectionMatrix");
	loc_primitive_color = glGetUniformLocation(h_ShaderProgram, "u_primitive_color");
}

void initialize_OpenGL(void) {
	glEnable(GL_MULTISAMPLE);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	glClearColor(28 / 255.0f, 255 / 255.0f, 245 / 255.0f, 1.0f);
	ViewMatrix = glm::mat4(1.0f);
}

void prepare_scene(void) {
	prepare_axes();
	prepare_airplane();
	prepare_sword();
	prepare_flame();
	prepare_crosshair();
	prepare_car2();
	prepare_rocket();
	prepare_shirt();
	prepare_house();
}

void initialize_renderer(void) {
	register_callbacks();
	prepare_shader_program();
	initialize_OpenGL();
	prepare_scene();
}

void initialize_glew(void) {
	GLenum error;

	glewExperimental = GL_TRUE;

	error = glewInit();
	if (error != GLEW_OK) {
		fprintf(stderr, "Error: %s\n", glewGetErrorString(error));
		exit(-1);
	}
	fprintf(stdout, "*********************************************************\n");
	fprintf(stdout, " - GLEW version supported: %s\n", glewGetString(GLEW_VERSION));
	fprintf(stdout, " - OpenGL renderer: %s\n", glGetString(GL_RENDERER));
	fprintf(stdout, " - OpenGL version supported: %s\n", glGetString(GL_VERSION));
	fprintf(stdout, "*********************************************************\n\n");
}

void greetings(char* program_name, char messages[][256], int n_message_lines) {
	fprintf(stdout, "**************************************************************\n\n");
	fprintf(stdout, "  PROGRAM NAME: %s\n\n", program_name);
	fprintf(stdout, "    This program was coded for CSE4170 students\n");
	fprintf(stdout, "      of Dept. of Comp. Sci. & Eng., Sogang University.\n\n");

	for (int i = 0; i < n_message_lines; i++)
		fprintf(stdout, "%s\n", messages[i]);
	fprintf(stdout, "\n**************************************************************\n\n");

	initialize_glew();
}

#define N_MESSAGE_LINES 2
int main(int argc, char* argv[]) {
	char program_name[64] = "Sogang CSE4170 Simple2DTransformationMotion_GLSL_3.0.3";
	char messages[N_MESSAGE_LINES][256] = {
		"    - Keys used: 'ESC'"
		"    - Mouse used: L-click and move"
	};

	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_RGBA | GLUT_MULTISAMPLE);
	glutInitWindowSize(1200, 800);
	glutInitContextVersion(3, 3);
	glutInitContextProfile(GLUT_CORE_PROFILE);
	glutCreateWindow(program_name);

	greetings(program_name, messages, N_MESSAGE_LINES);
	initialize_renderer();

	glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_GLUTMAINLOOP_RETURNS);
	glutMainLoop();
}


