#include <Catalpa.h>


class CatalpaBox : public Catalpa::Application {
public:
	CatalpaBox() {

	}
	
	~CatalpaBox() {
		
	}
};

Catalpa::Application* Catalpa::CreateApplication() {
	return new CatalpaBox();
}