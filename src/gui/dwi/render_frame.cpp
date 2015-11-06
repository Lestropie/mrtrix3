/*
    Copyright 2008 Brain Research Institute, Melbourne, Australia

    Written by J-Donald Tournier, 27/06/08.

    This file is part of MRtrix.

    MRtrix is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    MRtrix is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with MRtrix.  If not, see <http://www.gnu.org/licenses/>.


    24-10-2008 J-Donald Tournier <d.tournier@brain.org.au>
    * scale plot by SH(0,0) coefficient when normalise option is set

*/

#include <fstream>

#include <QGLWidget>

#include "app.h"
#include "gui/dwi/render_frame.h"

#define ROTATION_INC 0.004
#define D2R 0.01745329252

#define DIST_INC 0.005
#define DIST_MIN 0.1
#define DIST_MAX 10.0

#define SCALE_INC 1.05
#define SCALE_MIN 0.01
#define SCALE_MAX 10.0

#define ANGLE_INC 0.1
#define ANGLE_MIN 1.0
#define ANGLE_MAX 90.0

namespace MR
{
  namespace GUI
  {
    namespace DWI
    {

      RenderFrame::RenderFrame (QWidget* parent) :
        GL::Area (parent),
        view_angle (40.0), distance (0.3), line_width (1.0), scale (1.0), 
        lmax_computed (0), lod_computed (0), recompute_mesh (true), recompute_amplitudes (true), 
        show_axes (true), hide_neg_lobes (true), color_by_dir (true), use_lighting (true), 
        normalise (false), font (parent->font()), projection (this, font),
        orientation (Math::Versorf::unit()),
        focus (0.0, 0.0, 0.0), OS (0), OS_x (0), OS_y (0),
        renderer ((QGLWidget*)this)
      {
        setMinimumSize (128, 128);
        lighting = new GL::Lighting (this);
        lighting->set_background = true;
        connect (lighting, SIGNAL (changed()), this, SLOT (update()));
      }

      RenderFrame::~RenderFrame()
      {
        Context::Grab context (this);
        axes_VB.clear();
        axes_VAO.clear();
      }





      void RenderFrame::set_rotation (const GL::mat4& rotation)
      {
        Eigen::Matrix<float, 3, 3> M = Eigen::Matrix<float, 3, 3>::Zero();
        for (size_t i = 0; i != 3; ++i) {
          for (size_t j = 0; j != 3; ++j)
            M(i,j) = rotation(i,j);
        }
        orientation = Math::Versorf (M);
        update();
      }





      void RenderFrame::initializeGL ()
      {
        GL::init();
        renderer.initGL();
        gl::Enable (gl::DEPTH_TEST);

        axes_VB.gen();
        axes_VAO.gen();
        axes_VB.bind (gl::ARRAY_BUFFER);
        axes_VAO.bind();
        gl::EnableVertexAttribArray (0);
        gl::VertexAttribPointer (0, 3, gl::FLOAT, gl::FALSE_, 6*sizeof(GLfloat), (void*)0);

        gl::EnableVertexAttribArray (1);
        gl::VertexAttribPointer (1, 3, gl::FLOAT, gl::FALSE_, 6*sizeof(GLfloat), (void*) (3*sizeof(GLfloat)));

        GLfloat axis_data[] = {
          -1.0, -1.0, -1.0,   1.0, 0.0, 0.0,
           1.0, -1.0, -1.0,   1.0, 0.0, 0.0,
          -1.0, -1.0, -1.0,   0.0, 1.0, 0.0,
          -1.0,  1.0, -1.0,   0.0, 1.0, 0.0,
          -1.0, -1.0, -1.0,   0.0, 0.0, 1.0,
          -1.0, -1.0,  1.0,   0.0, 0.0, 1.0
        };
        gl::BufferData (gl::ARRAY_BUFFER, sizeof(axis_data), axis_data, gl::STATIC_DRAW);

        GL::Shader::Vertex vertex_shader (
            "layout(location = 0) in vec3 vertex_in;\n"
            "layout(location = 1) in vec3 color_in;\n"
            "uniform mat4 MVP;\n"
            "uniform vec3 origin;\n"
            "out vec3 color;\n"
            "void main () {\n"
            "  color = color_in;\n"
            "  gl_Position = MVP * vec4(vertex_in + origin, 1.0);\n"
            "}\n");

        GL::Shader::Fragment fragment_shader (
            "in vec3 color;\n"
            "out vec4 color_out;\n"
            "void main () {\n"
            "  color_out = vec4 (color, 1.0);\n"
            "}\n");

        axes_shader.attach (vertex_shader);
        axes_shader.attach (fragment_shader);
        axes_shader.link();

        INFO ("DWI renderer successfully initialised");
      }



      void RenderFrame::resizeGL (int w, int h)
      {
        projection.set_viewport (*this, 0, 0, w, h);
      }



      void RenderFrame::paintGL ()
      {
        gl::ColorMask (true, true, true, true); 
        gl::ClearColor (lighting->background_color[0], lighting->background_color[1], lighting->background_color[2], 0.0);
        gl::Clear (gl::COLOR_BUFFER_BIT | gl::DEPTH_BUFFER_BIT);

        float dist (1.0f / (distance * view_angle * D2R));
        float near_ = (dist-3.0f > 0.001f ? dist-3.0f : 0.001f);
        float horizontal = 2.0f * near_ * tan (0.5f*view_angle*D2R) * float (width()) / float (width()+height());
        float vertical = 2.0f * near_ * tan (0.5f*view_angle*D2R) * float (height()) / float (width()+height());

        GL::mat4 P;
        if (OS > 0) {
          float incx = 2.0f * horizontal / float (OS);
          float incy = 2.0f * vertical / float (OS);
          P = GL::frustum (-horizontal+OS_x*incx, -horizontal+ (1+OS_x) *incx, -vertical+OS_y*incy, -vertical+ (1+OS_y) *incy, near_, dist+3.0);
        }
        else {
          P = GL::frustum (-horizontal, horizontal, -vertical, vertical, near_, dist+3.0);
        }

        Eigen::Matrix<float, 4, 4> M;
        M.topLeftCorner (3, 3) = orientation.matrix().transpose();
        M(0,3) = M(1,3) = M(2,3) = M(3,0) = M(3,1) = M(3,2) = 0.0f;
        M(3,3) = 1.0f;

        GL::mat4 MV = GL::translate (0.0, 0.0, -dist) * GL::mat4 (M);
        projection.set (MV, P);

        gl::DepthMask (gl::TRUE_);

        if (values.size()) {
          if (std::isfinite (values[0])) {
            gl::Disable (gl::BLEND);

            float final_scale = scale;
            if (normalise && std::isfinite (values[0]) && values[0] != 0.0)
              final_scale /= values[0];

            renderer.start (projection, *lighting, final_scale, use_lighting, color_by_dir, hide_neg_lobes);

            if (recompute_mesh) {
              renderer.sh.update_mesh (lod_computed, lmax_computed);
              recompute_mesh = false;
            }

            if (recompute_amplitudes) {
              Eigen::Matrix<float, Eigen::Dynamic, 1> r_del_daz;
              const size_t nSH = Math::SH::NforL (lmax_computed);
              if (size_t(values.rows()) < nSH) {
                Eigen::Matrix<float, Eigen::Dynamic, 1> new_values = Eigen::Matrix<float, Eigen::Dynamic, 1>::Zero (nSH);
                new_values.topRows (values.rows()) = values;
                std::swap (values, new_values);
              }
              renderer.sh.compute_r_del_daz (r_del_daz, values.topRows (Math::SH::NforL (lmax_computed)));
              renderer.sh.set_data (r_del_daz);
              recompute_amplitudes = false;
            }

            renderer.draw (focus);
            renderer.stop();
          }
        }

        if (show_axes) {
          gl::LineWidth (line_width);
          gl::BlendFunc (gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
          gl::Enable (gl::BLEND);
          gl::Enable (gl::LINE_SMOOTH);

          axes_shader.start();
          gl::Uniform3fv (gl::GetUniformLocation (axes_shader, "origin"), 1, focus.data());
          gl::UniformMatrix4fv (gl::GetUniformLocation (axes_shader, "MVP"), 1, gl::FALSE_, projection.modelview_projection());
          axes_VAO.bind();
          gl::DrawArrays (gl::LINES, 0, 6);
          axes_shader.stop();

          gl::Disable (gl::BLEND);
          gl::Disable (gl::LINE_SMOOTH);
        }

        // need to clear alpha channel when using QOpenGLWidget (Qt >= 5.4)
        // otherwise we get transparent windows...
#if QT_VERSION >= 0x050400
        gl::ClearColor (0.0, 0.0, 0.0, 1.0);
        gl::ColorMask (false, false, false, true); 
        gl::Clear (gl::COLOR_BUFFER_BIT);
#endif

        if (OS > 0) snapshot();

      }








      void RenderFrame::mouseDoubleClickEvent (QMouseEvent* event)
      {
        if (event->modifiers() == Qt::NoModifier) {
          if (event->buttons() == Qt::LeftButton) {
            orientation = Math::Versorf::unit();
            update();
          }
          else if (event->buttons() == Qt::MidButton) {
            focus.setZero();
            update();
          }
        }
      }


      void RenderFrame::mousePressEvent (QMouseEvent* event)
      {
        last_pos = event->pos();
      }

      void RenderFrame::mouseMoveEvent (QMouseEvent* event)
      {
        int dx = event->x() - last_pos.x();
        int dy = event->y() - last_pos.y();
        last_pos = event->pos();
        if (dx == 0 && dy == 0) return;

        if (event->modifiers() == Qt::NoModifier) {
          if (event->buttons() == Qt::LeftButton) {
            const Eigen::Vector3f x = projection.screen_to_model_direction (QPoint (-dx, dy), focus);
            const Eigen::Vector3f z = projection.screen_normal();
            const Eigen::Vector3f v = x.cross (z).normalized();
            float angle = ROTATION_INC * std::sqrt (float (Math::pow2 (dx) + Math::pow2 (dy)));
            if (angle > Math::pi_2) angle = Math::pi_2;
            const Math::Versorf rot (angle, v);
            orientation = rot * orientation;
            update();
          }
          else if (event->buttons() == Qt::MidButton) {
            focus += projection.screen_to_model_direction (QPoint (dx, -dy), focus);
            update();
          }
          else if (event->buttons() == Qt::RightButton) {
            distance *= 1.0 - DIST_INC*dy;
            if (distance < DIST_MIN) distance = DIST_MIN;
            if (distance > DIST_MAX) distance = DIST_MAX;
            update();
          }
        }
        else if (event->modifiers() == Qt::ControlModifier) {
          if (event->buttons() == Qt::RightButton) {
            view_angle -= ANGLE_INC*dy;
            if (view_angle < ANGLE_MIN) view_angle = ANGLE_MIN;
            if (view_angle > ANGLE_MAX) view_angle = ANGLE_MAX;
            update();
          }
        }
      }

      void RenderFrame::wheelEvent (QWheelEvent* event)
      {
        int scroll = event->delta() / 120;
        for (int n = 0; n < scroll; n++) scale *= SCALE_INC;
        for (int n = 0; n > scroll; n--) scale /= SCALE_INC;
        if (scale > SCALE_MAX) scale = SCALE_MAX;
        if (scale < SCALE_MIN) scale = SCALE_MIN;
        update();
      }


      void RenderFrame::screenshot (int oversampling, const std::string& image_name)
      {
        QApplication::setOverrideCursor (Qt::BusyCursor);
        screenshot_name = image_name;
        OS = oversampling;
        OS_x = OS_y = 0;
        framebuffer.reset (new GLubyte [3*projection.width()*projection.height()]);
        pix.reset (new QImage (OS*projection.width(), OS*projection.height(), QImage::Format_RGB32));
        update();
      }



      void RenderFrame::snapshot ()
      {
        makeCurrent();
        gl::PixelStorei (gl::PACK_ALIGNMENT, 1);
        gl::ReadPixels (0, 0, projection.width(), projection.height(), gl::RGB, gl::UNSIGNED_BYTE, framebuffer.get());

        int start_i = projection.width()*OS_x;
        int start_j = projection.height()* (OS-OS_y-1);
        for (int j = 0; j < projection.height(); j++) {
          int j2 = projection.height()-j-1;
          for (int i = 0; i < projection.width(); i++) {
            GLubyte* p = framebuffer.get() + 3* (i+projection.width()*j);
            pix->setPixel (start_i + i, start_j + j2, qRgb (p[0], p[1], p[2]));
          }
        }

        if (OS_x == OS-1 && OS_y == OS-1)
          pix->save (screenshot_name.c_str(), "PNG");

        OS_x++;
        if (OS_x >= OS) {
          OS_x = 0;
          OS_y++;
          if (OS_y >= OS) {
            pix = NULL;
            framebuffer.reset();
            OS = OS_x = OS_y = 0;
            QApplication::restoreOverrideCursor();
          }
        }

        update();
      }



    }
  }
}

