#include "binding-util.h"
#include "disposable-binding.h"
#include "sceneelement-binding.h"
#include "sprite.h"
#include "sharedstate.h"
#include "binding-types.h"
#include "bitmap.h"
#include <vitaGL.h>

DEF_TYPE(Sprite);

RB_METHOD(spriteVitaInitialize)
{
    RB_UNUSED_PARAM;

    GFX_LOCK;

Sprite *s = new Sprite(nullptr);
setPrivateData(self, s);

/*
 * Ora possiamo inizializzare davvero src_rect/color/tone,
 * perché il Bitmap minimale fornisce le dipendenze richieste.
 */
s->initDynAttribs();

rb_iv_set(self, "viewport", Qnil);
rb_iv_set(self, "bitmap", Qnil);

    GFX_UNLOCK;

    return self;
}

DEF_GFX_PROP_I(Sprite, X)
DEF_GFX_PROP_I(Sprite, Y)
DEF_GFX_PROP_I(Sprite, OX)
DEF_GFX_PROP_I(Sprite, OY)
DEF_GFX_PROP_I(Sprite, Opacity)

DEF_GFX_PROP_F(Sprite, ZoomX)
DEF_GFX_PROP_OBJ_REF(
    Sprite,
    Bitmap,
    Bitmap,
    "bitmap"
)
DEF_GFX_PROP_F(Sprite, ZoomY)
DEF_GFX_PROP_F(Sprite, Angle)

RB_METHOD(spriteVitaDrawTest)
{
    RB_UNUSED_PARAM;

    Sprite *sprite =
        getPrivateData<Sprite>(self);

    sprite->vitaDrawTest();

    /*
     * Mostra realmente sul display quello appena disegnato.
     */
    vglSwapBuffers(GL_FALSE);

    return self;
}

void spriteBindingInitVitaMinimal()
{
    VALUE klass = rb_define_class("Sprite", rb_cObject);

    rb_define_alloc_func(
        klass,
        classAllocate<&SpriteType>
    );

    disposableBindingInit<Sprite>(klass);

    sceneElementBindingInit<Sprite>(klass);

    _rb_define_method(
        klass,
        "initialize",
        spriteVitaInitialize
    );

_rb_define_method(
    klass,
    "vita_draw_test",
    spriteVitaDrawTest
);

INIT_PROP_BIND(
    Sprite,
    Bitmap,
    "bitmap"
);
    INIT_PROP_BIND(Sprite, X, "x");
    INIT_PROP_BIND(Sprite, Y, "y");
    INIT_PROP_BIND(Sprite, OX, "ox");
    INIT_PROP_BIND(Sprite, OY, "oy");
    INIT_PROP_BIND(Sprite, Opacity, "opacity");

    INIT_PROP_BIND(Sprite, ZoomX, "zoom_x");
    INIT_PROP_BIND(Sprite, ZoomY, "zoom_y");
    INIT_PROP_BIND(Sprite, Angle, "angle");
}
