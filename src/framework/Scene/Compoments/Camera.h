#pragma once

#include "Core/Rect.h"
#include "Core/Transform.h"

#include <memory>
#include <mat4x4.hpp>
#include <ext/matrix_transform.hpp>

// class Camera0 {
//     enum Mode {
//         FIRST_PERSON,
//         THIRD_PERSON
//     };
//
// public:
//     Camera0();
//
//     ~Camera0();
//
//     struct {
//         bool up{false};
//         bool down{false};
//         bool left{false};
//         bool right{false};
//     } keys;
//
//     struct {
//         bool left{false};
//         bool middle{false};
//         bool right{false};
//     } mouseButtons;
//
//     bool flipY{false};
//
//     struct {
//         glm::mat4 view;
//         glm::mat4 perspective;
//     } matrices;
//
//     glm::vec3 position{0}, rotation{0};
//     float     mMoveSpeed{1};
//
//     void  updateViewMatrix();
//     void  update(float deltaTime);
//     bool  moving() const;
//     void  setPerspective(float fov, float aspect, float zNear, float zFar);
//     void  setRotation(glm::vec3 rotation);
//     void  setRotationByCamFront(glm::vec3 camFront);
//     void  setTranslation(glm::vec3 translation);
//     void  translate(const glm::vec3& delta);
//     void  rotate(const glm::vec3& delta);
//     void  setMoveSpeed(float moveSpeed);
//     float getMoveSpeed() const;
//     float rotationSpeed{1};
//
// private:
//     Mode mode{FIRST_PERSON};
//
//     bool dirty{false};
//
//     glm::vec3 cameraFront;
//
//     float zNear{0.01}, zFar{10}, fov{45}, aspect{1};
//
//     // bool rotating{false};
// };

struct Frustum {
    Frustum() {}

    union {
        glm::vec3 points[8];

        struct
        {
            glm::vec3 nearBottomLeft;
            glm::vec3 nearBottomRight;
            glm::vec3 nearTopLeft;
            glm::vec3 nearTopRight;

            glm::vec3 farBottomLeft;
            glm::vec3 farBottomRight;
            glm::vec3 farTopLeft;
            glm::vec3 farTopRight;
        } data;
    };

    void transform(const glm::mat4& matrix);
    BBox getBBox() const;
};

struct Ray {
};

/**
* Default assumption for screen coordinates is (0, 0) in lower left corner, (screenWidth, screenHeight) in upper right corner.
* The viewport is described in screen coordinates.
*/
class Camera {
public:
    Camera();
    ~Camera();

    void update(float deltaTime);
    void onShowInEditor();

    std::string getName() const { return "Camera"; }

    void setOrthographic(float screenWidth, float screenHeight, float zn, float zf) noexcept;
    void setPerspective(float fovY, float screenWidth, float screenHeight, float zn, float zf) noexcept;
    void setViewport(float x, float y, float width, float height) noexcept;
    void resize(float screenWidth, float screenHeight);

    void lookAt(const glm::vec3& target, const glm::vec3& worldUp = glm::vec3(0.f, 1.f, 0.f)) const noexcept { m_transform->lookAt(target, worldUp); }

    const glm::mat4& viewProj() const noexcept { return m_viewProj; }

    void updateViewMatrix() noexcept;
    void updateProjMatrix() noexcept;

    Transform* getTransform() const noexcept { return m_transform.get(); }

    void strafe(float d) const noexcept { m_transform->strafe(d); }

    void walk(float d) const noexcept { m_transform->walk(d); }

    void pitch(float angle) const noexcept { m_transform->pitch(flipY ? -angle : angle); }

    void rotateY(float angle) const noexcept { m_transform->rotateY(angle); }

    void roll(float angle) const noexcept { m_transform->roll(angle); }

    glm::vec3 getPosition() const noexcept { return m_transform->getPosition(); }

    // void setTranslation(const glm::vec3& p) const noexcept { m_transform->setPosition(p); }
    void setTranslation(glm::vec3 p) const noexcept { m_transform->setPosition(p); }

    void setTranslation(float x, float y, float z) const noexcept { m_transform->setPosition(glm::vec3(x, y, z)); }

    void translate(const glm::vec3& v) const noexcept { m_transform->move(v); }

    void zoom(float val) const noexcept { walk(val); }

    float getNearClipPlane() const { return m_nearZ; }

    float getFarClipPlane() const { return m_farZ; }

    glm::vec3 getRight() const noexcept { return m_transform->getRight(); }

    glm::vec3 getUp() const noexcept { return m_transform->getUp(); }

    glm::vec3 getForward() const noexcept { return m_transform->getForward(); }

    const glm::mat4& view() const noexcept { return m_view; }

    const glm::mat4& proj() const noexcept { return m_proj; }

    const glm::mat4& viewInverse() const noexcept { return m_viewInv; }

    const glm::mat4& projInverse() const noexcept { return m_projInv; }

    const glm::mat4& viewProjInv() const noexcept { return m_viewProjInv; }

    float getScreenWidth() const noexcept { return m_screenWidth; }

    float getScreenHeight() const noexcept { return m_screenHeight; }

    const Rect& getViewport() const noexcept { return m_viewport; }

    void setProj(glm::mat4& proj) { m_proj = proj; }

    void setView(glm::mat4& view) { m_view = view; }

    void setFlipY(bool flip) { flipY = flip; flipYTemp = flip; }

    glm::ivec2 getScreenSize() const noexcept { return glm::ivec2(m_screenWidth, m_screenHeight); }
    /**
    * Flips y coordinate in screen space.
    */
    glm::vec3 flip_y(const glm::vec3& p) const { return glm::vec3(p.x, m_screenHeight - p.y, p.z); }

    /**
    * Screen point has 3 components where x, y are in screen space and z describes the position in world units relative to the camera.
    */
    glm::vec3 screenToWorldPoint(const glm::vec3& p) const noexcept;

    glm::vec4 getPerspectiveParams() const noexcept { return glm::vec4(m_fovY, m_aspect, m_nearZ, m_farZ); }

    /**
    * Screen point has 3 components where x, y are in screen space and z describes the position in world units relative to the camera.
    */
    glm::vec3 screenToNDC(const glm::vec3& p) const noexcept;
    glm::vec3 worldToScreenPoint(const glm::vec3& worldPoint) const noexcept;
    glm::vec3 screenToViewportPoint(const glm::vec3& p) const noexcept;
    glm::vec3 viewportToScreenPoint(const glm::vec3& p) const noexcept;
    glm::vec3 viewportToNDC(const glm::vec3& p) const noexcept;
    glm::vec3 viewportToWorldPoint(const glm::vec3& p) const noexcept;
    glm::vec3 worldToViewportPoint(const glm::vec3& p) const noexcept;
    Ray       screenPointToRay(const glm::vec3& p) const noexcept;
    Ray       viewportPointToRay(const glm::vec3& p) const noexcept;

    float getHorizontalFOV() const { return m_fovY * m_aspect; }

    float getVerticalFOV() const { return m_fovY; }

    Frustum getFrustum() const;
    Frustum getFrustum(float nearZ, float farZ) const;

    bool  moving() const;
    void  setPerspective(float fov, float aspect, float zNear, float zFar);
    void  setRotation(glm::vec3 rotation);
    void  setRotation(glm::quat rotation);
    void  setMoveSpeed(float moveSpeed);
    float getMoveSpeed() const;
    void setScreenSize(float screenWidth, float screenHeight);
    struct {
        bool up{false};
        bool down{false};
        bool left{false};
        bool right{false};
    } keys;

    struct {
        bool left{false};
        bool middle{false};
        bool right{false};
    } mouseButtons;
    float mMoveSpeed{1};
    bool  flipY{false};
    bool  flipYTemp{false};
    bool  useInverseDepth{false};

protected:
    bool m_perspective{true};
    bool firstUpdate{true};

    float m_nearZ{0.f};
    float m_farZ{100.f};
    float m_aspect{1.f};
    float m_fovY{1.f};

    float m_screenWidth{1920.f};
    float m_screenHeight{1080.f};

    glm::mat4 m_view;
    glm::mat4 m_proj;
    glm::mat4 m_viewProj;
    glm::mat4 m_viewInv;
    glm::mat4 m_projInv;
    glm::mat4 m_viewProjInv;

    Rect                       m_viewport;
    Rect                       m_normalizedViewport;
    std::unique_ptr<Transform> m_transform;
};

// using Camera = Camera;
