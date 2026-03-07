#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <cmath>
#include <algorithm>

#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/Popup.hpp>
#include <matjson.hpp>
#include <fstream>

using namespace geode::prelude;

class $modify(MyEditorUI, EditorUI) {
    
    void onImgToGD(CCObject*) {
        auto selected = this->getSelectedObjects();
        
        // Validación: Solo permitir un objeto ancla seleccionado
        if (selected && selected->count() == 1) {
            
            auto anchorObj = static_cast<GameObject*>(selected->objectAtIndex(0));
            CCPoint anchorPos = anchorObj->getPosition();
            auto editorUI = this;

            geode::createQuickPopup(
                "PhotoShoot Mode",
                "Which type of file would you like to import?",
                "Image", "JSON",
                [editorUI, anchorPos](auto, bool isJson) {
                    
                    char filename[MAX_PATH] = {0};
                    OPENFILENAMEA ofn;
                    ZeroMemory(&ofn, sizeof(ofn));
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = NULL;
                    
                    if (isJson) {
                        ofn.lpstrFilter = "JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";
                        ofn.lpstrTitle = "Select a JSON for PhotoShoot";
                    } else {
                        ofn.lpstrFilter = "Images (*.png;*.jpg;*.jpeg)\0*.png;*.jpg;*.jpeg\0All Files (*.*)\0*.*\0";
                        ofn.lpstrTitle = "Select an Image for PhotoShoot";
                    }

                    ofn.lpstrFile = filename;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

                    if (GetOpenFileNameA(&ofn)) {
                        
                        HWND hwnd = FindWindowA(NULL, "Geometry Dash");
                        if (hwnd) SetForegroundWindow(hwnd);

                        auto newObjects = CCArray::create();
                        int blocksCreated = 0;
                        const float stepSize = 7.5f;

                        // --- LA SOLUCIÓN HSV DEFINITIVA (Con Canal Dedicado) ---
                        auto createPixel = [&](float x, float y, ccColor3B color) {
                            CCPoint pos = { anchorPos.x + (x * stepSize), anchorPos.y - (y * stepSize) };
                            
                            // Creador oficial (soluciona guardado y selección)
                            auto obj = editorUI->m_editorLayer->createObject(211, pos, false);
                            
                            if (obj) {
                                obj->setScale(0.25f);
                                
                                // 1. Convertir el color RGB de la imagen a valores HSV
                                float r = color.r / 255.0f;
                                float g = color.g / 255.0f;
                                float b = color.b / 255.0f;

                                float cmax = std::max({r, g, b});
                                float cmin = std::min({r, g, b});
                                float delta = cmax - cmin;

                                float h = 0;
                                if (delta > 0) {
                                    if (cmax == r) h = 60.0f * std::fmod(((g - b) / delta), 6.0f);
                                    else if (cmax == g) h = 60.0f * (((b - r) / delta) + 2.0f);
                                    else if (cmax == b) h = 60.0f * (((r - g) / delta) + 4.0f);
                                }
                                if (h < 0) h += 360.0f;

                                float s = (cmax == 0) ? 0 : (delta / cmax);
                                float v = cmax;

                                // Geometry Dash usa un rango de Hue de -180 a 180
                                float h_shift = h > 180.0f ? h - 360.0f : h;

                                // 2. Inyectar el HSV en el canal 999 (El jugador lo pondrá en negro)
                                if (obj->m_baseColor) {
                                    obj->m_baseColor->m_colorID = 999; 
                                    obj->m_baseColor->m_hsv = { h_shift, s, v, true, true }; 
                                }
                                
                                // 3. Forzar al motor a leer las matemáticas
                                obj->m_baseUsesHSV = true;
                                
                                newObjects->addObject(obj);
                                blocksCreated++;
                            }
                        };

                        if (isJson) {
                            std::ifstream file(filename);
                            auto res = matjson::parse(file);

                            if (!res.ok()) return;

                            auto data = res.unwrap();
                            if (data.isArray()) {
                                for (auto const& item : data.asArray().unwrap()) { 
                                    if (item.contains("x") && item.contains("y") && item.contains("color")) {
                                        float x = static_cast<float>(item["x"].asDouble().unwrap());
                                        float y = static_cast<float>(item["y"].asDouble().unwrap());
                                        auto col = item["color"].asArray().unwrap();

                                        createPixel(x, y, { 
                                            static_cast<GLubyte>(col[0].asInt().unwrap()), 
                                            static_cast<GLubyte>(col[1].asInt().unwrap()), 
                                            static_cast<GLubyte>(col[2].asInt().unwrap()) 
                                        });
                                    }
                                }
                            }
                        } else {
                            auto image = new CCImage();
                            if (image->initWithImageFile(filename)) {
                                unsigned char* data = image->getData();
                                int width = image->getWidth();
                                int height = image->getHeight();
                                bool hasAlpha = image->hasAlpha();
                                int bpp = hasAlpha ? 4 : 3;

                                int step = (width > 150) ? (width / 150) : 1;
                                
                                for (int y = 0; y < height; y += step) {
                                    for (int x = 0; x < width; x += step) {
                                        int index = (y * width + x) * bpp;
                                        
                                        int r = data[index];
                                        int g = data[index + 1];
                                        int b = data[index + 2];
                                        int a = hasAlpha ? data[index + 3] : 255;

                                        if (a > 20) {
                                            createPixel((float)x, (float)y, {(GLubyte)r, (GLubyte)g, (GLubyte)b});
                                        }
                                    }
                                }
                                image->release();
                            }
                        }

                        if (blocksCreated > 0) {
                            editorUI->deselectAll();
                            editorUI->selectObjects(newObjects, false);
                            
                            // Mensaje final actualizado indicando el truco del canal
                            FLAlertLayer::create(
                                "PhotoShoot Success", 
                                fmt::format("Created <cg>{}</c> blocks.\n\n<cy>IMPORTANT:</c> Open the color menu and set <cg>Channel 999</c> to BLACK for the colors to work!", blocksCreated).c_str(), 
                                "Got it!"
                            )->show();
                        }

                    } else {
                        HWND hwnd = FindWindowA(NULL, "Geometry Dash");
                        if (hwnd) SetForegroundWindow(hwnd);
                    }
                }
            );

        } else {
            FLAlertLayer::create("PhotoShoot", "Please select exactly one object to use as an anchor.", "OK")->show();
        }
    }

    void createMoveMenu() {
        EditorUI::createMoveMenu();
        auto btnSprite = CCSprite::createWithSpriteFrameName("GJ_plus3Btn_001.png");
        btnSprite->setScale(0.8f);
        auto* btn = CCMenuItemSpriteExtra::create(btnSprite, this, menu_selector(MyEditorUI::onImgToGD));
        if (m_editButtonBar && m_editButtonBar->m_buttonArray) {
            m_editButtonBar->m_buttonArray->addObject(btn);
            auto rows = GameManager::sharedState()->getIntGameVariable("0049");
            auto columns = GameManager::sharedState()->getIntGameVariable("0050");
            m_editButtonBar->reloadItems(rows, columns);
        }
    }
};