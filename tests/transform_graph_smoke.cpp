#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>
#include <cstdlib>

int main()
{
	glm::mat4 parent = glm::translate(glm::mat4(1.f), glm::vec3(1, 0, 0));
	glm::mat4 childLocal = glm::rotate(glm::mat4(1.f), 0.25f, glm::vec3(0, 1, 0));
	glm::mat4 world = parent * childLocal;
	glm::vec4 p = world * glm::vec4(0, 0, 0, 1);
	if (glm::length(glm::vec3(p) - glm::vec3(1, 0, 0)) > 1e-4f)
	{
		fprintf(stderr, "transform smoke failed\n");
		return 1;
	}
	return 0;
}
