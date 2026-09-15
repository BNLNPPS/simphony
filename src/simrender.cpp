#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include <argparse/argparse.hpp>

#include "CSGOptiX/CSGOptiX.h"
#include "g4cx/G4CXOpticks.hh"
#include "sysrap/OPTICKS_LOG.hh"
#include "sysrap/SEventConfig.hh"
#include "sysrap/SFrameConfig.hh"
#include "sysrap/SGLM.h"

#include <QApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QWidget>

namespace
{
constexpr const char* CONTROLS = R"(simrender controls:
  left drag / arrows             orbit
  right drag / Shift+arrows      pan
  middle drag / PageUp/PageDown  dolly forward/back
  mouse wheel / +/-              zoom in/out
  W/S                            forward/back
  A/D                            left/right
  Q/E                            up/down
  Shift+WASDQE                   faster movement
  1/2                            front/back orthographic views
  3/4                            left/right orthographic views
  5/6                            top/bottom orthographic views
  H                              restore the initial camera
  O                              perspective/orthographic
  X                              normal/depth display
  K                              save current frame as NPY
  P                              print camera state
  F1                             print these controls
  Esc                            close
)";

QPointF mousePosition(const QMouseEvent* event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->localPos();
#endif
}

class SimRenderWidget final : public QWidget
{
  public:
    explicit SimRenderWidget(CSGOptiX* renderer, QWidget* parent = nullptr) :
        QWidget(parent),
        renderer_(renderer),
        camera_(renderer ? renderer->sglm : nullptr)
    {
        if (renderer_ == nullptr || camera_ == nullptr)
        {
            throw std::runtime_error("simrender requires an initialized OptiX renderer");
        }

        home_eye_ = SGLM::EYE;
        home_look_ = SGLM::LOOK;
        home_up_ = SGLM::UP;
        home_zoom_ = SGLM::ZOOM;
        home_camera_ = camera_->cam;

        setFocusPolicy(Qt::StrongFocus);
        setMinimumSize(320, 240);
        resize(renderer_->getRenderWidth(), renderer_->getRenderHeight());
        std::cout << CONTROLS;
        renderFrame();
    }

  protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        if (image_.isNull())
            return;

        QSize target_size = image_.size();
        target_size.scale(size(), Qt::KeepAspectRatio);
        const QPoint top_left(
            (width() - target_size.width()) / 2,
            (height() - target_size.height()) / 2);
        painter.drawImage(QRect(top_left, target_size), image_);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton &&
            event->button() != Qt::RightButton &&
            event->button() != Qt::MiddleButton)
        {
            return;
        }

        drag_start_ = ndc(mousePosition(event));
        drag_rotation_ = camera_->q_eyerot;
        drag_shift_ = camera_->eyeshift;
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        const Qt::MouseButtons buttons = event->buttons();
        if (!(buttons & (Qt::LeftButton | Qt::RightButton | Qt::MiddleButton)))
            return;

        const glm::vec2 current = ndc(mousePosition(event));
        const glm::vec2 delta = current - drag_start_;

        if (buttons & Qt::LeftButton)
        {
            orbit(drag_start_, current, drag_rotation_);
        }
        else if (buttons & Qt::RightButton)
        {
            pan(drag_shift_, delta.x, delta.y);
        }
        else
        {
            dolly(drag_shift_, -delta.y);
        }

        event->accept();
    }

    void wheelEvent(QWheelEvent* event) override
    {
        const float steps = static_cast<float>(event->angleDelta().y()) / 120.f;
        zoom(steps);
        event->accept();
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        const bool shift = event->modifiers() & Qt::ShiftModifier;
        unsigned   navigation = 0u;
        switch (event->key())
        {
        case Qt::Key_W:
            navigation = SGLM_Modnav::MOD_W;
            break;
        case Qt::Key_A:
            navigation = SGLM_Modnav::MOD_A;
            break;
        case Qt::Key_S:
            navigation = SGLM_Modnav::MOD_S;
            break;
        case Qt::Key_D:
            navigation = SGLM_Modnav::MOD_D;
            break;
        case Qt::Key_Q:
            navigation = SGLM_Modnav::MOD_Q;
            break;
        case Qt::Key_E:
            navigation = SGLM_Modnav::MOD_E;
            break;
        default:
            break;
        }

        if (navigation != 0u)
        {
            if (shift)
            {
                navigation |= SGLM_Modifiers::MOD_SHIFT;
            }
            camera_->key_pressed_action(navigation);
            cameraChanged();
            event->accept();
            return;
        }

        switch (event->key())
        {
        case Qt::Key_Left:
            if (shift)
                pan(camera_->eyeshift, -KEY_STEP, 0.f);
            else
                orbit(glm::vec2(0.f), glm::vec2(-KEY_STEP, 0.f), camera_->q_eyerot);
            break;
        case Qt::Key_Right:
            if (shift)
                pan(camera_->eyeshift, KEY_STEP, 0.f);
            else
                orbit(glm::vec2(0.f), glm::vec2(KEY_STEP, 0.f), camera_->q_eyerot);
            break;
        case Qt::Key_Up:
            if (shift)
                pan(camera_->eyeshift, 0.f, KEY_STEP);
            else
                orbit(glm::vec2(0.f), glm::vec2(0.f, KEY_STEP), camera_->q_eyerot);
            break;
        case Qt::Key_Down:
            if (shift)
                pan(camera_->eyeshift, 0.f, -KEY_STEP);
            else
                orbit(glm::vec2(0.f), glm::vec2(0.f, -KEY_STEP), camera_->q_eyerot);
            break;
        case Qt::Key_PageUp:
            dolly(camera_->eyeshift, KEY_STEP);
            break;
        case Qt::Key_PageDown:
            dolly(camera_->eyeshift, -KEY_STEP);
            break;
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            zoom(1.f);
            break;
        case Qt::Key_Minus:
            zoom(-1.f);
            break;
        case Qt::Key_1:
            setAxisView(glm::vec3(0.f, -1.f, 0.f), glm::vec3(0.f, 0.f, 1.f), "front");
            break;
        case Qt::Key_2:
            setAxisView(glm::vec3(0.f, 1.f, 0.f), glm::vec3(0.f, 0.f, 1.f), "back");
            break;
        case Qt::Key_3:
            setAxisView(glm::vec3(-1.f, 0.f, 0.f), glm::vec3(0.f, 0.f, 1.f), "left");
            break;
        case Qt::Key_4:
            setAxisView(glm::vec3(1.f, 0.f, 0.f), glm::vec3(0.f, 0.f, 1.f), "right");
            break;
        case Qt::Key_5:
            setAxisView(glm::vec3(0.f, 0.f, 1.f), glm::vec3(0.f, 1.f, 0.f), "top");
            break;
        case Qt::Key_6:
            setAxisView(glm::vec3(0.f, 0.f, -1.f), glm::vec3(0.f, -1.f, 0.f), "bottom");
            break;
        case Qt::Key_H:
            restoreHome();
            break;
        case Qt::Key_O:
            camera_->tcam();
            cameraChanged();
            break;
        case Qt::Key_X:
            camera_->toggle_rendertype();
            cameraChanged();
            break;
        case Qt::Key_K:
            renderer_->render_save("simrender");
            break;
        case Qt::Key_P:
            std::cout << camera_->desc() << std::flush;
            break;
        case Qt::Key_F1:
            std::cout << CONTROLS << std::flush;
            break;
        case Qt::Key_Escape:
            close();
            break;
        default:
            QWidget::keyPressEvent(event);
            return;
        }
        event->accept();
    }

  private:
    static constexpr float KEY_STEP = 0.04f;

    glm::vec2 ndc(const QPointF& point) const
    {
        return glm::vec2(
            2.f * static_cast<float>(point.x()) / static_cast<float>(std::max(width(), 1)) - 1.f,
            1.f - 2.f * static_cast<float>(point.y()) / static_cast<float>(std::max(height(), 1)));
    }

    void orbit(const glm::vec2& from, const glm::vec2& to, const glm::quat& rotation)
    {
        camera_->q_eyerot = SGLM_Arcball::A2B_Screen(from, to) * rotation;
        view_name_ = "custom";
        cameraChanged();
    }

    void pan(const glm::vec3& shift, float horizontal, float vertical)
    {
        const float scale = camera_->extent();
        camera_->eyeshift = shift + glm::vec3(horizontal * scale, vertical * scale, 0.f);
        cameraChanged();
    }

    void dolly(const glm::vec3& shift, float distance)
    {
        camera_->eyeshift = shift + glm::vec3(0.f, 0.f, distance * camera_->extent());
        cameraChanged();
    }

    void zoom(float steps)
    {
        const float zoom = std::clamp(SGLM::ZOOM * std::pow(1.15f, steps), 0.05f, 100.f);
        SGLM::SetZOOM(zoom);
        cameraChanged();
    }

    void setAxisView(const glm::vec3& direction, const glm::vec3& up, const char* name)
    {
        const glm::vec3 look(home_look_);
        const float     distance = std::max(glm::length(glm::vec3(home_eye_) - look), 1.f);
        const glm::vec3 eye = look + direction * distance;

        SGLM::SetEYE(eye.x, eye.y, eye.z);
        SGLM::SetLOOK(look.x, look.y, look.z);
        SGLM::SetUP(up.x, up.y, up.z);
        camera_->cam = CAM_ORTHOGRAPHIC;
        camera_->home();
        view_name_ = name;
        cameraChanged();
    }

    void restoreHome()
    {
        SGLM::SetEYE(home_eye_.x, home_eye_.y, home_eye_.z);
        SGLM::SetLOOK(home_look_.x, home_look_.y, home_look_.z);
        SGLM::SetUP(home_up_.x, home_up_.y, home_up_.z);
        camera_->cam = home_camera_;
        camera_->home();
        SGLM::SetZOOM(home_zoom_);
        view_name_ = "home";
        cameraChanged();
    }

    void cameraChanged()
    {
        camera_->update();
        renderFrame();
    }

    void renderFrame()
    {
        const unsigned char* pixels = renderer_->renderFrame();
        if (pixels == nullptr)
        {
            throw std::runtime_error("OptiX did not produce a pixel buffer");
        }

        // The fourth render channel stores depth, not opacity. RGBX makes Qt
        // display the normal/depth RGB channels without alpha blending.
        image_ = QImage(
            pixels,
            renderer_->getRenderWidth(),
            renderer_->getRenderHeight(),
            renderer_->getRenderWidth() * 4,
            QImage::Format_RGBX8888);

        const char* mode = camera_->rendertype == 0 ? "normal" : "depth";
        setWindowTitle(QString("simrender | %1 | %2/%3 | %4 | %5 ms")
                           .arg(QString::fromStdString(camera_->get_frame_name()))
                           .arg(SCAM::Name(camera_->cam))
                           .arg(view_name_)
                           .arg(mode)
                           .arg(renderer_->kernel_dt * 1000., 0, 'f', 2));
        update();
    }

    CSGOptiX*   renderer_;
    SGLM*       camera_;
    QImage      image_;
    glm::vec2   drag_start_{0.f};
    glm::quat   drag_rotation_{1.f, 0.f, 0.f, 0.f};
    glm::vec3   drag_shift_{0.f};
    glm::vec4   home_eye_{0.f};
    glm::vec4   home_look_{0.f};
    glm::vec4   home_up_{0.f};
    float       home_zoom_{1.f};
    int         home_camera_{CAM_PERSPECTIVE};
    const char* view_name_{"home"};
};

void applyVectorOption(const argparse::ArgumentParser& program, const char* option)
{
    if (!program.is_used(option))
        return;

    const glm::vec3 value = SGLM::SVec3(program.get<std::string>(option).c_str(), 0.f);
    if (std::string(option) == "--eye")
    {
        SGLM::SetEYE(value.x, value.y, value.z);
    }
    else if (std::string(option) == "--look")
    {
        SGLM::SetLOOK(value.x, value.y, value.z);
    }
    else
    {
        SGLM::SetUP(value.x, value.y, value.z);
    }
}
} // namespace

int main(int argc, char** argv)
{
    OPTICKS_LOG(argc, argv);

    argparse::ArgumentParser program("simrender", "0.1.0");
    program.add_description("Interactively inspect Simphony's OptiX geometry using normal or depth rendering.");

    program.add_argument("-g", "--gdml")
        .help("path to the input GDML file")
        .nargs(1);
    program.add_argument("--controls").help("print interactive controls and exit").flag();
    program.add_argument("--width")
        .help("OptiX render width")
        .default_value(1280)
        .scan<'i', int>();
    program.add_argument("--height")
        .help("OptiX render height")
        .default_value(720)
        .scan<'i', int>();
    program.add_argument("-f", "--frame")
        .help("geometry frame/MOI specification")
        .default_value(std::string("-1"))
        .nargs(1);
    program.add_argument("--camera")
        .help("initial camera: perspective or orthographic")
        .default_value(std::string("perspective"))
        .nargs(1);
    program.add_argument("--eye").help("initial eye vector, for example -1,-1,0").nargs(1);
    program.add_argument("--look").help("initial look vector, for example 0,0,0").nargs(1);
    program.add_argument("--up").help("initial up vector, for example 0,0,1").nargs(1);
    program.add_argument("--depth").help("start in depth mode instead of normal mode").flag();

    try
    {
        program.parse_args(argc, argv);
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n'
                  << program;
        return EXIT_FAILURE;
    }

    if (program.get<bool>("--controls"))
    {
        std::cout << CONTROLS;
        return EXIT_SUCCESS;
    }
    if (!program.is_used("--gdml"))
    {
        std::cerr << "--gdml is required\n"
                  << program;
        return EXIT_FAILURE;
    }

    const std::filesystem::path gdml = program.get<std::string>("--gdml");
    const int                   width = program.get<int>("--width");
    const int                   height = program.get<int>("--height");
    const std::string           camera = program.get<std::string>("--camera");

    if (!std::filesystem::is_regular_file(gdml))
    {
        std::cerr << "GDML file does not exist: " << gdml << '\n';
        return EXIT_FAILURE;
    }
    if (width < 1 || height < 1)
    {
        std::cerr << "--width and --height must be positive\n";
        return EXIT_FAILURE;
    }
    if (camera != "perspective" && camera != "orthographic")
    {
        std::cerr << "--camera must be perspective or orthographic\n";
        return EXIT_FAILURE;
    }

    SGLM::SetWH(width, height);
    SGLM::SetCAM(camera.c_str());
    applyVectorOption(program, "--eye");
    applyVectorOption(program, "--look");
    applyVectorOption(program, "--up");
    SFrameConfig::SetFrameMask("pixel");
    SEventConfig::SetRGModeRender();
    SEventConfig::Initialize();

    QApplication app(argc, argv);
    app.setApplicationName("simrender");

    G4CXOpticks* g4cx = G4CXOpticks::SetGeometry(gdml.c_str());
    if (g4cx == nullptr || g4cx->cx == nullptr)
    {
        std::cerr << "No CUDA/OptiX device is available for rendering\n";
        return EXIT_FAILURE;
    }

    g4cx->cx->setFrame(program.get<std::string>("--frame").c_str());
    if (program.get<bool>("--depth"))
    {
        g4cx->cx->sglm->rendertype = 1;
        g4cx->cx->sglm->update();
    }

    SimRenderWidget viewer(g4cx->cx);
    viewer.show();
    return app.exec();
}
