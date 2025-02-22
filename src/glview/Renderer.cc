#include "Renderer.h"
#include "PolySet.h"
#include "Polygon2d.h"
#include "ColorMap.h"
#include "printutils.h"
#include "PlatformUtils.h"
#include "system-gl.h"

#include <Eigen/LU>
#include <fstream>

#ifndef NULLGL

Renderer::Renderer()
{
  PRINTD("Renderer() start");

  renderer_shader.progid = 0;

  // Setup default colors
  // The main colors, MATERIAL and CUTOUT, come from this object's
  // colorscheme. Colorschemes don't currently hold information
  // for Highlight/Background colors
  // but it wouldn't be too hard to make them do so.

  // MATERIAL is set by this object's colorscheme
  // CUTOUT is set by this object's colorscheme
  colormap[ColorMode::HIGHLIGHT] = {255, 81, 81, 128};
  colormap[ColorMode::BACKGROUND] = {180, 180, 180, 128};
  // MATERIAL_EDGES is set by this object's colorscheme
  // CUTOUT_EDGES is set by this object's colorscheme
  colormap[ColorMode::HIGHLIGHT_EDGES] = {255, 171, 86, 128};
  colormap[ColorMode::BACKGROUND_EDGES] = {150, 150, 150, 128};

  Renderer::setColorScheme(ColorMap::inst()->defaultColorScheme());

  std::string vs_str = Renderer::loadShaderSource("Preview.vert");
  std::string fs_str = Renderer::loadShaderSource("Preview.frag");
  const char *vs_source = vs_str.c_str();
  const char *fs_source = fs_str.c_str();

  GLint status;
  GLenum err;
  auto vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vs, 1, (const GLchar **)&vs_source, nullptr);
  glCompileShader(vs);
  err = glGetError();
  if (err != GL_NO_ERROR) {
    PRINTDB("OpenGL Error: %s\n", gluErrorString(err));
    return;
  }
  glGetShaderiv(vs, GL_COMPILE_STATUS, &status);
  if (status == GL_FALSE) {
    int loglen;
    char logbuffer[1000];
    glGetShaderInfoLog(vs, sizeof(logbuffer), &loglen, logbuffer);
    PRINTDB("OpenGL Program Compile Vertex Shader Error:\n%s", logbuffer);
    return;
  }

  auto fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fs, 1, (const GLchar **)&fs_source, nullptr);
  glCompileShader(fs);
  err = glGetError();
  if (err != GL_NO_ERROR) {
    PRINTDB("OpenGL Error: %s\n", gluErrorString(err));
    return;
  }
  glGetShaderiv(fs, GL_COMPILE_STATUS, &status);
  if (status == GL_FALSE) {
    int loglen;
    char logbuffer[1000];
    glGetShaderInfoLog(fs, sizeof(logbuffer), &loglen, logbuffer);
    PRINTDB("OpenGL Program Compile Fragment Shader Error:\n%s", logbuffer);
    return;
  }

  auto edgeshader_prog = glCreateProgram();
  glAttachShader(edgeshader_prog, vs);
  glAttachShader(edgeshader_prog, fs);
  glLinkProgram(edgeshader_prog);

  err = glGetError();
  if (err != GL_NO_ERROR) {
    PRINTDB("OpenGL Error: %s\n", gluErrorString(err));
    return;
  }

  glGetProgramiv(edgeshader_prog, GL_LINK_STATUS, &status);
  if (status == GL_FALSE) {
    int loglen;
    char logbuffer[1000];
    glGetProgramInfoLog(edgeshader_prog, sizeof(logbuffer), &loglen, logbuffer);
    PRINTDB("OpenGL Program Linker Error:\n%s", logbuffer);
    return;
  }

  int loglen;
  char logbuffer[1000];
  glGetProgramInfoLog(edgeshader_prog, sizeof(logbuffer), &loglen, logbuffer);
  if (loglen > 0) {
    PRINTDB("OpenGL Program Link OK:\n%s", logbuffer);
  }
  glValidateProgram(edgeshader_prog);
  glGetProgramInfoLog(edgeshader_prog, sizeof(logbuffer), &loglen, logbuffer);
  if (loglen > 0) {
    PRINTDB("OpenGL Program Validation results:\n%s", logbuffer);
  }

  renderer_shader.progid = edgeshader_prog; // 0
  renderer_shader.type = EDGE_RENDERING;
  renderer_shader.data.csg_rendering.color_area = glGetUniformLocation(edgeshader_prog, "color1"); // 1
  renderer_shader.data.csg_rendering.color_edge = glGetUniformLocation(edgeshader_prog, "color2"); // 2
  renderer_shader.data.csg_rendering.barycentric = glGetAttribLocation(edgeshader_prog, "barycentric"); // 3

  PRINTD("Renderer() end");
}

void Renderer::resize(int /*w*/, int /*h*/)
{
}

bool Renderer::getColor(Renderer::ColorMode colormode, Color4f& col) const
{
  if (colormode == ColorMode::NONE) return false;
  if (colormap.count(colormode) > 0) {
    col = colormap.at(colormode);
    return true;
  }
  return false;
}

std::string Renderer::loadShaderSource(const std::string& name) {
  std::string shaderPath = (PlatformUtils::resourcePath("shaders") / name).string();
  std::ostringstream buffer;
  std::ifstream f(shaderPath);
  if (f.is_open()) {
    buffer << f.rdbuf();
  } else {
    LOG(message_group::UI_Error, "Cannot open shader source file: '%1$s'", shaderPath);
  }
  return buffer.str();
}

Renderer::csgmode_e Renderer::get_csgmode(const bool highlight_mode, const bool background_mode, const OpenSCADOperator type) const {
  int csgmode = highlight_mode ? CSGMODE_HIGHLIGHT : (background_mode ? CSGMODE_BACKGROUND : CSGMODE_NORMAL);
  if (type == OpenSCADOperator::DIFFERENCE) csgmode |= CSGMODE_DIFFERENCE_FLAG;
  return csgmode_e(csgmode);
}

void Renderer::setColor(const float color[4], const shaderinfo_t *shaderinfo) const
{
  if (shaderinfo && shaderinfo->type != EDGE_RENDERING) {
    return;
  }

  PRINTD("setColor a");
  Color4f col;
  getColor(ColorMode::MATERIAL, col);
  float c[4] = {color[0], color[1], color[2], color[3]};
  if (c[0] < 0) c[0] = col[0];
  if (c[1] < 0) c[1] = col[1];
  if (c[2] < 0) c[2] = col[2];
  if (c[3] < 0) c[3] = col[3];
  glColor4fv(c);
#ifdef ENABLE_OPENCSG
  if (shaderinfo) {
    glUniform4f(shaderinfo->data.csg_rendering.color_area, c[0], c[1], c[2], c[3]);
    glUniform4f(shaderinfo->data.csg_rendering.color_edge, (c[0] + 1) / 2, (c[1] + 1) / 2, (c[2] + 1) / 2, 1.0);
  }
#endif
}

// returns the color which has been set, which may differ from the color input parameter
Color4f Renderer::setColor(ColorMode colormode, const float color[4], const shaderinfo_t *shaderinfo) const
{
  PRINTD("setColor b");
  Color4f basecol;
  if (getColor(colormode, basecol)) {
    if (colormode == ColorMode::BACKGROUND || colormode != ColorMode::HIGHLIGHT) {
      basecol = {color[0] >= 0 ? color[0] : basecol[0],
                 color[1] >= 0 ? color[1] : basecol[1],
                 color[2] >= 0 ? color[2] : basecol[2],
                 color[3] >= 0 ? color[3] : basecol[3]};
    }
    setColor(basecol.data(), shaderinfo);
  }
  return basecol;
}

void Renderer::setColor(ColorMode colormode, const shaderinfo_t *shaderinfo) const
{
  PRINTD("setColor c");
  float c[4] = {-1, -1, -1, -1};
  setColor(colormode, c, shaderinfo);
}

/* fill this->colormap with matching entries from the colorscheme. note
   this does not change Highlight or Background colors as they are not
   represented in the colorscheme (yet). Also edgecolors are currently the
   same for CGAL & OpenCSG */
void Renderer::setColorScheme(const ColorScheme& cs) {
  PRINTD("setColorScheme");
  colormap[ColorMode::MATERIAL] = ColorMap::getColor(cs, RenderColor::OPENCSG_FACE_FRONT_COLOR);
  colormap[ColorMode::CUTOUT] = ColorMap::getColor(cs, RenderColor::OPENCSG_FACE_BACK_COLOR);
  colormap[ColorMode::MATERIAL_EDGES] = ColorMap::getColor(cs, RenderColor::CGAL_EDGE_FRONT_COLOR);
  colormap[ColorMode::CUTOUT_EDGES] = ColorMap::getColor(cs, RenderColor::CGAL_EDGE_BACK_COLOR);
  colormap[ColorMode::EMPTY_SPACE] = ColorMap::getColor(cs, RenderColor::BACKGROUND_COLOR);
  this->colorscheme = &cs;
}

#ifdef ENABLE_OPENCSG
static void draw_triangle(const Renderer::shaderinfo_t *shaderinfo, const Vector3d& p0, const Vector3d& p1, const Vector3d& p2,
                          bool e0, bool e1, bool e2, double z, bool mirror)
{
  Renderer::shader_type_t type =
    (shaderinfo) ? shaderinfo->type : Renderer::NONE;

  // e0,e1,e2 are used to disable some edges from display.
  // Edges are numbered to correspond with the vertex opposite of them.
  // The edge shader draws edges when the minimum component of barycentric coords is near 0
  // Disabled edges have their corresponding components set to 1.0 when they would otherwise be 0.0.
  double d0 = e0 ? 0.0 : 1.0;
  double d1 = e1 ? 0.0 : 1.0;
  double d2 = e2 ? 0.0 : 1.0;

  switch (type) {
  case Renderer::EDGE_RENDERING:
    if (mirror) {
      glVertexAttrib3f(shaderinfo->data.csg_rendering.barycentric, 1.0, d1, d2);
      glVertex3f(p0[0], p0[1], p0[2] + z);
      glVertexAttrib3f(shaderinfo->data.csg_rendering.barycentric, d0, d1, 1.0);
      glVertex3f(p2[0], p2[1], p2[2] + z);
      glVertexAttrib3f(shaderinfo->data.csg_rendering.barycentric, d0, 1.0, d2);
      glVertex3f(p1[0], p1[1], p1[2] + z);
    } else {
      glVertexAttrib3f(shaderinfo->data.csg_rendering.barycentric, 1.0, d1, d2);
      glVertex3f(p0[0], p0[1], p0[2] + z);
      glVertexAttrib3f(shaderinfo->data.csg_rendering.barycentric, d0, 1.0, d2);
      glVertex3f(p1[0], p1[1], p1[2] + z);
      glVertexAttrib3f(shaderinfo->data.csg_rendering.barycentric, d0, d1, 1.0);
      glVertex3f(p2[0], p2[1], p2[2] + z);
    }
    break;
  default:
  case Renderer::SELECT_RENDERING:
    glVertex3d(p0[0], p0[1], p0[2] + z);
    if (!mirror) {
      glVertex3d(p1[0], p1[1], p1[2] + z);
    }
    glVertex3d(p2[0], p2[1], p2[2] + z);
    if (mirror) {
      glVertex3d(p1[0], p1[1], p1[2] + z);
    }
  }
}
#endif // ifdef ENABLE_OPENCSG

static void draw_tri(const Vector3d& p0, const Vector3d& p1, const Vector3d& p2, double z, bool mirror)
{
  glVertex3d(p0[0], p0[1], p0[2] + z);
  if (!mirror) glVertex3d(p1[0], p1[1], p1[2] + z);
  glVertex3d(p2[0], p2[1], p2[2] + z);
  if (mirror) glVertex3d(p1[0], p1[1], p1[2] + z);
}

static void gl_draw_triangle(const Renderer::shaderinfo_t *shaderinfo, const Vector3d& p0, const Vector3d& p1, const Vector3d& p2, bool e0, bool e1, bool e2, double z, bool mirrored)
{
  double ax = p1[0] - p0[0], bx = p1[0] - p2[0];
  double ay = p1[1] - p0[1], by = p1[1] - p2[1];
  double az = p1[2] - p0[2], bz = p1[2] - p2[2];
  double nx = ay * bz - az * by;
  double ny = az * bx - ax * bz;
  double nz = ax * by - ay * bx;
  double nl = sqrt(nx * nx + ny * ny + nz * nz);
  glNormal3d(nx / nl, ny / nl, nz / nl);
#ifdef ENABLE_OPENCSG
  if (shaderinfo) {
    draw_triangle(shaderinfo, p0, p1, p2, e0, e1, e2, z, mirrored);
  } else
#endif
  {
    draw_tri(p0, p1, p2, z, mirrored);
  }
}

void Renderer::render_surface(const PolySet& ps, csgmode_e csgmode, const Transform3d& m, const shaderinfo_t *shaderinfo) const
{
  PRINTD("Renderer render");
  bool mirrored = m.matrix().determinant() < 0;

  if (ps.getDimension() == 2) {
    // Render 2D objects 1mm thick, but differences slightly larger
    double zbase = 1 + ((csgmode & CSGMODE_DIFFERENCE_FLAG) ? 0.1 : 0);
    glBegin(GL_TRIANGLES);

    // Render top+bottom
    for (double z : {-zbase / 2, zbase / 2}) {
      for (const auto& poly : ps.indices) {
        if (poly.size() == 3) {
          if (z < 0) {
            gl_draw_triangle(shaderinfo, ps.vertices[poly.at(0)], ps.vertices[poly.at(2)], ps.vertices[poly.at(1)], true, true, true, z, mirrored);
          } else {
            gl_draw_triangle(shaderinfo, ps.vertices[poly.at(0)], ps.vertices[poly.at(1)], ps.vertices[poly.at(2)], true, true, true, z, mirrored);
          }
        } else if (poly.size() == 4) {
          if (z < 0) {
            gl_draw_triangle(shaderinfo, ps.vertices[poly.at(0)], ps.vertices[poly.at(3)], ps.vertices[poly.at(1)], false, true, true, z, mirrored);
            gl_draw_triangle(shaderinfo, ps.vertices[poly.at(2)], ps.vertices[poly.at(1)], ps.vertices[poly.at(3)], false, true, true, z, mirrored);
          } else {
            gl_draw_triangle(shaderinfo, ps.vertices[poly.at(0)], ps.vertices[poly.at(1)], ps.vertices[poly.at(3)], false, true, true, z, mirrored);
            gl_draw_triangle(shaderinfo, ps.vertices[poly.at(2)], ps.vertices[poly.at(3)], ps.vertices[poly.at(1)], false, true, true, z, mirrored);
          }
        } else {
          Vector3d center = Vector3d::Zero();
          for (const auto& point : poly) {
            center[0] += ps.vertices[point][0];
            center[1] += ps.vertices[point][1];
          }
          center /= poly.size();
          for (size_t j = 1; j <= poly.size(); ++j) {
            if (z < 0) {
              gl_draw_triangle(shaderinfo, center, ps.vertices[poly.at(j % poly.size())], ps.vertices[poly.at(j - 1)],
                               true, false, false, z, mirrored);
            } else {
              gl_draw_triangle(shaderinfo, center, ps.vertices[poly.at(j - 1)], ps.vertices[poly.at(j % poly.size())],
                               true, false, false, z, mirrored);
            }
          }
        }
      }
    }

    // Render sides
    if (ps.getPolygon().outlines().size() > 0) {
      for (const Outline2d& o : ps.getPolygon().outlines()) {
        for (size_t j = 1; j <= o.vertices.size(); ++j) {
          Vector3d p1(o.vertices[j - 1][0], o.vertices[j - 1][1], -zbase / 2);
          Vector3d p2(o.vertices[j - 1][0], o.vertices[j - 1][1], zbase / 2);
          Vector3d p3(o.vertices[j % o.vertices.size()][0], o.vertices[j % o.vertices.size()][1], -zbase / 2);
          Vector3d p4(o.vertices[j % o.vertices.size()][0], o.vertices[j % o.vertices.size()][1], zbase / 2);
          gl_draw_triangle(shaderinfo, p2, p1, p3, true, false, true, 0, mirrored);
          gl_draw_triangle(shaderinfo, p2, p3, p4, true, true, false, 0, mirrored);
        }
      }
    } else {
      // If we don't have borders, use the polygons as borders.
      // FIXME: When is this used?
      const std::vector<IndexedFace> *borders_p = &ps.indices;
      for (const auto& poly : *borders_p) {
        for (size_t j = 1; j <= poly.size(); ++j) {
          Vector3d p1 = ps.vertices[poly.at(j - 1)], p2 = ps.vertices[poly.at(j - 1)];
          Vector3d p3 = ps.vertices[poly.at(j % poly.size())], p4 = ps.vertices[poly.at(j % poly.size())];
          p1[2] -= zbase / 2, p2[2] += zbase / 2;
          p3[2] -= zbase / 2, p4[2] += zbase / 2;
          gl_draw_triangle(shaderinfo, p2, p1, p3, true, false, true, 0, mirrored);
          gl_draw_triangle(shaderinfo, p2, p3, p4, true, true, false, 0, mirrored);
        }
      }
    }
    glEnd();
  } else if (ps.getDimension() == 3) {
    for (const auto& poly : ps.indices) {
      glBegin(GL_TRIANGLES);
      if (poly.size() == 3) {
        gl_draw_triangle(shaderinfo, ps.vertices[poly.at(0)], ps.vertices[poly.at(1)], ps.vertices[poly.at(2)], true, true, true, 0, mirrored);
      } else if (poly.size() == 4) {
        gl_draw_triangle(shaderinfo, ps.vertices[poly.at(0)], ps.vertices[poly.at(1)], ps.vertices[poly.at(3)], false, true, true, 0, mirrored);
        gl_draw_triangle(shaderinfo, ps.vertices[poly.at(2)], ps.vertices[poly.at(3)], ps.vertices[poly.at(1)], false, true, true, 0, mirrored);
      } else {
        Vector3d center = Vector3d::Zero();
        for (const auto& point : poly) {
          center += ps.vertices[point];
        }
        center /= poly.size();
        for (size_t j = 1; j <= poly.size(); ++j) {
          gl_draw_triangle(shaderinfo, center, ps.vertices[poly.at(j - 1)], ps.vertices[poly.at(j % poly.size())], true, false, false, 0, mirrored);
        }
      }
      glEnd();
    }
  } else {
    assert(false && "Cannot render object with no dimension");
  }
}

/*! This is used in throwntogether and CGAL mode

   csgmode is set to CSGMODE_NONE in CGAL mode. In this mode a pure 2D rendering is performed.

   For some reason, this is not used to render edges in Preview mode
 */
void Renderer::render_edges(const PolySet& ps, csgmode_e csgmode) const
{
  glDisable(GL_LIGHTING);
  if (ps.getDimension() == 2) {
    if (csgmode == Renderer::CSGMODE_NONE) {
      // Render only outlines
      for (const Outline2d& o : ps.getPolygon().outlines()) {
        glBegin(GL_LINE_LOOP);
        for (const Vector2d& v : o.vertices) {
          glVertex3d(v[0], v[1], 0);
        }
        glEnd();
      }
    } else {
      // Render 2D objects 1mm thick, but differences slightly larger
      double zbase = 1 + ((csgmode & CSGMODE_DIFFERENCE_FLAG) ? 0.1 : 0);

      for (const Outline2d& o : ps.getPolygon().outlines()) {
        // Render top+bottom outlines
        for (double z : { -zbase / 2, zbase / 2}) {
          glBegin(GL_LINE_LOOP);
          for (const Vector2d& v : o.vertices) {
            glVertex3d(v[0], v[1], z);
          }
          glEnd();
        }
        // Render sides
        glBegin(GL_LINES);
        for (const Vector2d& v : o.vertices) {
          glVertex3d(v[0], v[1], -zbase / 2);
          glVertex3d(v[0], v[1], +zbase / 2);
        }
        glEnd();
      }
    }
  } else if (ps.getDimension() == 3) {
    for (const auto& polygon : ps.indices) {
      const IndexedFace *poly = &polygon;
      glBegin(GL_LINE_LOOP);
      for (const auto& ind : *poly) {
	Vector3d p=ps.vertices[ind];
        glVertex3d(p[0], p[1], p[2]);
      }
      glEnd();
    }
  } else {
    assert(false && "Cannot render object with no dimension");
  }
  glEnable(GL_LIGHTING);
}

#else //NULLGL

Renderer::Renderer() : colorscheme(nullptr) {}
void Renderer::resize(int /*w*/, int /*h*/) {}
bool Renderer::getColor(Renderer::ColorMode colormode, Color4f& col) const { return false; }
std::string Renderer::loadShaderSource(const std::string& name) { return ""; }
Renderer::csgmode_e Renderer::get_csgmode(const bool highlight_mode, const bool background_mode, const OpenSCADOperator type) const { return {}; }
void Renderer::setColor(const float color[4], const shaderinfo_t *shaderinfo) const {}
Color4f Renderer::setColor(ColorMode colormode, const float color[4], const shaderinfo_t *shaderinfo) const { return {}; }
void Renderer::setColor(ColorMode colormode, const shaderinfo_t *shaderinfo) const {}
void Renderer::setColorScheme(const ColorScheme& cs) {}
void Renderer::render_surface(const PolySet& ps, csgmode_e csgmode, const Transform3d& m, const shaderinfo_t *shaderinfo) const {}
void Renderer::render_edges(const PolySet& ps, csgmode_e csgmode) const {}

#endif //NULLGL
