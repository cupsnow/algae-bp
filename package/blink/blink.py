import pygame
import tkinter as tk
from tkinter import colorchooser

# -------------------------------------------------
# Configuration
# -------------------------------------------------

WIDTH = 1280
HEIGHT = 720

frequency = 2.0          # Hz
color1 = (0, 0, 0)
color2 = (255, 255, 255)

show_settings = True

pygame.init()
pygame.font.init()

screen = pygame.display.set_mode((WIDTH, HEIGHT))
pygame.display.set_caption("Color Flash")

clock = pygame.time.Clock()
font = pygame.font.SysFont(None, 28)

root = tk.Tk()
root.withdraw()

# -------------------------------------------------
# Edit freq
# -------------------------------------------------
frequency_text = f"{frequency:.1f}"

editing_frequency = False

freq_box = pygame.Rect(180, 35, 80, 32)

# -------------------------------------------------
# Buttons
# -------------------------------------------------

btn_apply = pygame.Rect(30, 210, 100, 40)
btn_hide = pygame.Rect(150, 210, 120, 40)
btn_color1 = pygame.Rect(180, 90, 40, 30)
btn_color2 = pygame.Rect(180, 140, 40, 30)

# -------------------------------------------------
# Timing
# -------------------------------------------------

start_time = pygame.time.get_ticks()

# -------------------------------------------------
# Helper
# -------------------------------------------------

def choose_color(current):
    rgb, _ = colorchooser.askcolor(current)
    if rgb is None:
        return current
    return tuple(int(x) for x in rgb)


def draw_settings():

    pygame.draw.rect(screen, (230,230,230), (20,20,280,250))
    pygame.draw.rect(screen, (0,0,0), (20,20,280,250), 2)

    screen.blit(font.render("Frequency (Hz)", True, (0,0,0)), (30,40))
    # screen.blit(font.render(f"{frequency:.1f}", True, (0,0,255)), (180,40))

    pygame.draw.rect(screen, (255,255,255), freq_box)
    pygame.draw.rect(screen, (0,0,0), freq_box, 2)

    text = frequency_text if editing_frequency else f"{frequency:.1f}"

    screen.blit(
        font.render(text, True, (0,0,0)),
        (freq_box.x + 5, freq_box.y + 4)
    )


    screen.blit(font.render("Color A", True, (0,0,0)), (30,95))
    pygame.draw.rect(screen, color1, btn_color1)

    screen.blit(font.render("Color B", True, (0,0,0)), (30,145))
    pygame.draw.rect(screen, color2, btn_color2)

    pygame.draw.rect(screen, (180,180,180), btn_apply)
    pygame.draw.rect(screen, (180,180,180), btn_hide)

    screen.blit(font.render("Apply", True, (0,0,0)), (45,220))
    screen.blit(font.render("Hide", True, (0,0,0)), (175,220))

    screen.blit(font.render("UP/DOWN : change frequency", True, (0,0,0)), (30,180))


# -------------------------------------------------
# Main loop
# -------------------------------------------------

running = True

while running:

    now = pygame.time.get_ticks()

    switch_interval = 1000.0 / (frequency * 2)

    n = int((now - start_time) / switch_interval)

    flash_color = color1 if (n % 2 == 0) else color2

    screen.fill(flash_color)

    for event in pygame.event.get():

        if event.type == pygame.QUIT:
            running = False

        elif event.type == pygame.KEYDOWN:

            if editing_frequency:

                if event.key == pygame.K_RETURN:

                    try:
                        frequency = max(0.1, float(frequency_text))
                        start_time = pygame.time.get_ticks()
                    except ValueError:
                        pass

                    editing_frequency = False

                elif event.key == pygame.K_ESCAPE:

                    frequency_text = f"{frequency:.1f}"
                    editing_frequency = False

                elif event.key == pygame.K_BACKSPACE:

                    frequency_text = frequency_text[:-1]

                else:

                    if event.unicode in "0123456789.":
                        frequency_text += event.unicode

                continue

            if event.key == pygame.K_ESCAPE:
                running = False

            elif event.key == pygame.K_SPACE:
                show_settings = not show_settings

            elif event.key == pygame.K_UP:
                frequency += 0.5
                frequency_text = f"{frequency:.1f}"
                start_time = pygame.time.get_ticks()

            elif event.key == pygame.K_DOWN:
                frequency = max(0.1, frequency - 0.5)
                frequency_text = f"{frequency:.1f}"
                start_time = pygame.time.get_ticks()

        elif event.type == pygame.MOUSEBUTTONDOWN:

            if not show_settings:
                show_settings = True
                continue

            if freq_box.collidepoint(event.pos):
                editing_frequency = True

            else:
                editing_frequency = False

            if btn_color1.collidepoint(event.pos):
                color1 = choose_color(color1)

            elif btn_color2.collidepoint(event.pos):
                color2 = choose_color(color2)

            elif btn_apply.collidepoint(event.pos):
                try:
                    frequency = max(0.1, float(frequency_text))
                except ValueError:
                    pass

                frequency_text = f"{frequency:.1f}"
                start_time = pygame.time.get_ticks()

            elif btn_hide.collidepoint(event.pos):
                show_settings = False

    if show_settings:
        draw_settings()

    pygame.display.flip()

    clock.tick(240)

pygame.quit()